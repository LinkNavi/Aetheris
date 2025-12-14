using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using OpenTK.Mathematics;
using System.Net;

namespace Aetheris
{
    public class Client
    {
        private readonly ConcurrentQueue<(TcpPacketType type, byte[] data)> tcpPacketQueue = new();
        private readonly Dictionary<int, TaskCompletionSource<byte[]>> pendingChunkRequests = new();
        private int nextRequestId = 0;
        private Game? game;
        private TcpClient? tcpRequest;      // For chunk requests only
        private TcpClient? tcpBroadcast;    // For server broadcasts only
        private NetworkStream? streamRequest;
        private NetworkStream? streamBroadcast;
        private UdpClient? udp;
        private IPEndPoint? serverUdpEndpoint;
        private NetworkStream? stream;
        private ServerInventoryManager inventoryManager;
        private readonly ConcurrentDictionary<(int, int, int), Aetheris.Chunk> loadedChunks = new();
        private readonly ConcurrentQueue<(int cx, int cy, int cz, float priority)> requestQueue = new();
        private readonly ConcurrentDictionary<(int, int, int), byte> requestedChunks = new();

        private Vector3 lastPlayerChunk = new Vector3(float.MinValue, float.MinValue, float.MinValue);
        private CancellationTokenSource? cts;
        private Task? loaderTask;
        private Task? updateTask;
        private Task? tcpListenerTask;

        private readonly SemaphoreSlim networkSemaphore = new SemaphoreSlim(1, 1);
        private readonly SemaphoreSlim connectionSemaphore = new SemaphoreSlim(1, 1);
        private int currentRenderDistance;
        private readonly int udpPort = ClientConfig.SERVER_PORT + 1;

        private enum TcpPacketType : byte
        {
            ChunkRequest = 0,
            BlockBreak = 1,
            BlockPlace = 2,         // NEW: Block placement
            InventorySync = 3,
            InventoryUpdate = 4,
            ItemPickup = 5
        }

        // Auto-tuned parameters
        public int MaxConcurrentLoads { get; set; } = 16;
        public int ChunksPerUpdateBatch { get; set; } = 128;
        public int UpdatesPerSecond { get; set; } = 20;
        public int MaxPendingUploads { get; set; } = 64;

        private int UpdateInterval => 1000 / UpdatesPerSecond;

        public void Run()
        {
            cts = new CancellationTokenSource();
            AutoTuneSettings();

            Task.Run(async () => await ConnectToServerAsync("127.0.0.1", ClientConfig.SERVER_PORT)).Wait();

            game = new Game(new Dictionary<(int, int, int), Aetheris.Chunk>(loadedChunks), this);
            loaderTask = Task.Run(() => ChunkLoaderLoopAsync(cts.Token));
            updateTask = Task.Run(() => ChunkUpdateLoopAsync(cts.Token));

            // ENABLE TCP broadcast listener for block breaks
            tcpListenerTask = Task.Run(() => TcpBroadcastListenerAsync(cts.Token));

            _ = Task.Run(() => ListenForUdpAsync(cts.Token));

            game.RunGame();
            Cleanup();
        }

        private void AutoTuneSettings()
        {
            int rd = ClientConfig.RENDER_DISTANCE;

            if (rd <= 4)
            {
                MaxConcurrentLoads = 4;
                ChunksPerUpdateBatch = 32;
                UpdatesPerSecond = 10;
                MaxPendingUploads = 16;
            }
            else if (rd <= 8)
            {
                MaxConcurrentLoads = 8;
                ChunksPerUpdateBatch = 64;
                UpdatesPerSecond = 15;
                MaxPendingUploads = 32;
            }
            else if (rd <= 16)
            {
                MaxConcurrentLoads = 16;
                ChunksPerUpdateBatch = 128;
                UpdatesPerSecond = 20;
                MaxPendingUploads = 64;
            }
            else
            {
                MaxConcurrentLoads = 32;
                ChunksPerUpdateBatch = 256;
                UpdatesPerSecond = 30;
                MaxPendingUploads = 128;
            }

            Console.WriteLine($"[Client] Auto-tuned for {rd} chunk render distance: " +
                            $"{MaxConcurrentLoads} concurrent, {ChunksPerUpdateBatch} batch size");
        }


        private async Task ConnectToServerAsync(string host, int port)
        {
            Console.WriteLine($"[Client] Connecting to {host}:{port}...");

            // Connection 1: For chunk requests (request/response)
            tcpRequest = new TcpClient();
            await tcpRequest.ConnectAsync(host, port);
            streamRequest = tcpRequest.GetStream();
            tcpRequest.NoDelay = true;
            tcpRequest.SendTimeout = 5000;
            tcpRequest.ReceiveTimeout = 5000;

            Console.WriteLine("[Client] Request stream connected");

            // Connection 2: For broadcasts (receive-only)
            tcpBroadcast = new TcpClient();
            await tcpBroadcast.ConnectAsync(host, port);
            streamBroadcast = tcpBroadcast.GetStream();
            tcpBroadcast.NoDelay = true;

            // CRITICAL: Send 0xFF to identify as broadcast listener
            byte[] broadcastMarker = new byte[] { 0xFF };
            await streamBroadcast.WriteAsync(broadcastMarker, 0, 1);
            await streamBroadcast.FlushAsync();

            Console.WriteLine("[Client] Broadcast stream connected and registered");

            // UDP
            udp = new UdpClient();
            serverUdpEndpoint = new IPEndPoint(IPAddress.Parse(host), udpPort);
            udp.Connect(serverUdpEndpoint);

            Console.WriteLine("[Client] Connected to server (TCP request + TCP broadcast + UDP)");
        }

        // TCP Broadcast Listener - listens for server-initiated messages
        private async Task TcpBroadcastListenerAsync(CancellationToken token)
        {
            Console.WriteLine("[Client] TCP broadcast listener started");

            try
            {
                while (!token.IsCancellationRequested)
                {
                    if (streamBroadcast == null || tcpBroadcast == null || !tcpBroadcast.Connected)
                    {
                        await Task.Delay(100, token);
                        continue;
                    }

                    try
                    {
                        // Read packet type (blocking is OK here)
                        var packetTypeBuf = new byte[1];
                        int bytesRead = await streamBroadcast.ReadAsync(packetTypeBuf, 0, 1, token);

                        if (bytesRead == 0)
                        {
                            Console.WriteLine("[Client] Server closed broadcast connection");
                            break;
                        }

                        TcpPacketType packetType = (TcpPacketType)packetTypeBuf[0];

                        if (packetType == TcpPacketType.BlockBreak)
                        {
                            await HandleBlockBreakBroadcastAsync(token);
                        }
                        else if (packetType == TcpPacketType.BlockPlace)
                        {
                            await HandleBlockPlaceBroadcastAsync(token);
                        }
                        else
                        {
                            Console.WriteLine($"[Client] Unknown broadcast packet type: {packetType}");
                        }
                    }
                    catch (Exception ex) when (!(ex is OperationCanceledException))
                    {
                        Console.WriteLine($"[Client] TCP listener error: {ex.Message}");
                        await Task.Delay(1000, token);
                    }
                }
            }
            catch (OperationCanceledException)
            {
                Console.WriteLine("[Client] TCP broadcast listener cancelled");
            }
        }
private BlockType ConvertByteToBlockType(byte blockTypeByte)
{
    return blockTypeByte switch
    {
        1 => BlockType.Stone,
        2 => BlockType.Dirt,
        3 => BlockType.Grass,
        4 => BlockType.Sand,
        5 => BlockType.Snow,
        6 => BlockType.Gravel,
        7 => BlockType.Wood,
        8 => BlockType.Leaves,
        _ => BlockType.Stone
    };
}
        private async Task HandleBlockPlaceBroadcastAsync(CancellationToken token)
        {
            try
            {
                // Read 13 bytes (12 for coordinates + 1 for block type)
                var buf = new byte[13];
                await ReadFullAsync(streamBroadcast!, buf, 0, 13, token);

                int x = BitConverter.ToInt32(buf, 0);
                int y = BitConverter.ToInt32(buf, 4);
                int z = BitConverter.ToInt32(buf, 8);
                byte blockType = buf[12];

                Console.WriteLine($"[Client] ===== RECEIVED BLOCK PLACE BROADCAST =====");
                Console.WriteLine($"[Client] Position: ({x}, {y}, {z}), BlockType: {blockType}");

                // Apply the block placement locally
                // Place a SOLID CUBE
                for (int dx = -1; dx <= 1; dx++)
                {
                    for (int dy = -1; dy <= 1; dy++)
                    {
                        for (int dz = -1; dz <= 1; dz++)
                        {
                            int px = x + dx;
                            int py = y + dy;
                            int pz = z + dz;

                            float dist = MathF.Sqrt(dx * dx + dy * dy + dz * dz);
                            float strength = 10f * (1f - Math.Clamp(dist / 1.5f, 0f, 1f));

                            if (strength > 0.1f)
                            {
                                WorldGen.AddDensityModification(px, py, pz, strength);
                            }
                        }
                    }
                }

                // Set block type
                BlockType serverBlockType = ConvertByteToBlockType(blockType);
                WorldGen.SetBlock(x, y, z, serverBlockType);

                // Calculate affected chunks
                float placeRadius = 2f;
                int affectRadius = (int)Math.Ceiling(placeRadius);
                var chunksToReload = new HashSet<(int, int, int)>();

                for (int dx = -affectRadius; dx <= affectRadius; dx++)
                {
                    for (int dy = -affectRadius; dy <= affectRadius; dy++)
                    {
                        for (int dz = -affectRadius; dz <= affectRadius; dz++)
                        {
                            int worldX = x + dx;
                            int worldY = y + dy;
                            int worldZ = z + dz;

                            int chunkX = worldX / ClientConfig.CHUNK_SIZE;
                            int chunkY = worldY / ClientConfig.CHUNK_SIZE_Y;
                            int chunkZ = worldZ / ClientConfig.CHUNK_SIZE;

                            chunksToReload.Add((chunkX, chunkY, chunkZ));
                        }
                    }
                }

                Console.WriteLine($"[Client] Need to reload {chunksToReload.Count} chunks");

                await Task.Delay(100, token);

                foreach (var (chunkX, chunkY, chunkZ) in chunksToReload)
                {
                    ForceReloadChunk(chunkX, chunkY, chunkZ);
                }

                Console.WriteLine($"[Client] Finished processing block place broadcast");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[Client] Error in HandleBlockPlaceBroadcastAsync: {ex.Message}");
                throw;
            }
        }

        public async Task SendBlockPlaceAsync(int x, int y, int z, byte blockType)
        {
            if (streamRequest == null || tcpRequest == null || !tcpRequest.Connected)
            {
                Console.WriteLine("[Client] Cannot send block place - not connected");
                return;
            }

            await networkSemaphore.WaitAsync();
            try
            {
                // Send 14 bytes (1 byte packet type + 12 bytes coordinates + 1 byte block type)
                byte[] packet = new byte[14];
                packet[0] = (byte)TcpPacketType.BlockPlace;

                BitConverter.TryWriteBytes(packet.AsSpan(1, 4), x);
                BitConverter.TryWriteBytes(packet.AsSpan(5, 4), y);
                BitConverter.TryWriteBytes(packet.AsSpan(9, 4), z);
                packet[13] = blockType; // Add block type

                await streamRequest.WriteAsync(packet, 0, packet.Length);
                await streamRequest.FlushAsync();

                Console.WriteLine($"[Client] ===== SENT BLOCK PLACE =====");
                Console.WriteLine($"[Client] Position: ({x}, {y}, {z}), BlockType: {blockType}");
                Console.WriteLine($"[Client] Packet size: 14 bytes");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[Client] Error sending block place: {ex.Message}");
            }
            finally
            {
                networkSemaphore.Release();
            }
        }
        public void ForceReloadChunk(int cx, int cy, int cz)
        {
            var coord = (cx, cy, cz);

            Console.WriteLine($"[Client] ForceReloadChunk called for ({cx}, {cy}, {cz})");

            // Step 1: Remove from client-side caches
            bool wasLoaded = loadedChunks.TryRemove(coord, out _);
            bool wasRequested = requestedChunks.TryRemove(coord, out _);

            Console.WriteLine($"[Client]   - Was loaded: {wasLoaded}, was requested: {wasRequested}");

            // Step 2: CRITICAL - Clear GPU mesh BEFORE re-requesting
            game?.Renderer.ClearChunkMesh(cx, cy, cz);

            // Step 3: Remove from request queue
            var tempQueue = new List<(int cx, int cy, int cz, float priority)>();
            while (requestQueue.TryDequeue(out var item))
            {
                if (item.cx != cx || item.cy != cy || item.cz != cz)
                {
                    tempQueue.Add(item);
                }
            }

            foreach (var item in tempQueue)
            {
                requestQueue.Enqueue(item);
            }

            // Step 4: FORCE IMMEDIATE REQUEST
            requestedChunks.TryRemove(coord, out _);
            requestQueue.Enqueue((cx, cy, cz, 0.0f));

            Console.WriteLine($"[Client]   - Queued for immediate reload (queue size: {requestQueue.Count})");

            // Step 5: CRITICAL - Process this request immediately in background
            _ = Task.Run(async () =>
            {
                try
                {
                    await Task.Delay(50); // Small delay to let any in-flight requests finish

                    var chunk = (cx, cy, cz, 0.0f);
                    await LoadChunkAsync(chunk, CancellationToken.None);

                    Console.WriteLine($"[Client] Successfully reloaded chunk ({cx}, {cy}, {cz})");
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[Client] Error force-reloading chunk: {ex.Message}");
                }
            });
        }
        public async Task RequestInventorySyncAsync()
        {
            if (stream == null || tcpRequest == null || !tcpRequest.Connected) return;

            await networkSemaphore.WaitAsync();
            try
            {
                byte[] packet = new byte[1];
                packet[0] = (byte)TcpPacketType.InventorySync;

                await stream.WriteAsync(packet, 0, packet.Length);
                await stream.FlushAsync();

                // Receive inventory data
                var lengthBuf = new byte[4];
                await ReadFullAsync(stream, lengthBuf, 0, 4, CancellationToken.None);
                int dataLength = BitConverter.ToInt32(lengthBuf, 0);

                var inventoryData = new byte[dataLength];
                await ReadFullAsync(stream, inventoryData, 0, dataLength, CancellationToken.None);

                // Deserialize and update local inventory


                if (game?.player is { } p)
                    p.Inventory = inventoryManager.DeserializeInventory(inventoryData);

            }
            finally
            {
                networkSemaphore.Release();
            }
        }

        private async Task HandleBlockBreakBroadcastAsync(CancellationToken token)
        {
            try
            {
                // Read exactly 12 bytes for coordinates
                var buf = new byte[12];
                await ReadFullAsync(streamBroadcast!, buf, 0, 12, token);

                int x = BitConverter.ToInt32(buf, 0);
                int y = BitConverter.ToInt32(buf, 4);
                int z = BitConverter.ToInt32(buf, 8);

                Console.WriteLine($"[Client] ===== RECEIVED BLOCK BREAK BROADCAST =====");
                Console.WriteLine($"[Client] Position: ({x}, {y}, {z})");

                // Calculate affected chunks
                float miningRadius = 5.0f;
                int affectRadius = (int)Math.Ceiling(miningRadius);
                var chunksToReload = new HashSet<(int, int, int)>();

                for (int dx = -affectRadius; dx <= affectRadius; dx++)
                {
                    for (int dy = -affectRadius; dy <= affectRadius; dy++)
                    {
                        for (int dz = -affectRadius; dz <= affectRadius; dz++)
                        {
                            int worldX = x + dx;
                            int worldY = y + dy;
                            int worldZ = z + dz;

                            int chunkX = worldX / ClientConfig.CHUNK_SIZE;
                            int chunkY = worldY / ClientConfig.CHUNK_SIZE_Y;
                            int chunkZ = worldZ / ClientConfig.CHUNK_SIZE;

                            chunksToReload.Add((chunkX, chunkY, chunkZ));
                        }
                    }
                }

                Console.WriteLine($"[Client] Need to reload {chunksToReload.Count} chunks");

                // Small delay to let any in-flight chunk requests complete
                await Task.Delay(100, token);

                foreach (var (chunkX, chunkY, chunkZ) in chunksToReload)
                {
                    Console.WriteLine($"[Client] Force reloading chunk ({chunkX}, {chunkY}, {chunkZ})");
                    ForceReloadChunk(chunkX, chunkY, chunkZ);
                }

                Console.WriteLine($"[Client] Finished processing block break broadcast");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[Client] Error in HandleBlockBreakBroadcastAsync: {ex.Message}");
                throw;
            }
        }
        private void InvalidateChunksAroundBlock(int x, int y, int z)
        {
            int cx = x / ClientConfig.CHUNK_SIZE;
            int cy = y / ClientConfig.CHUNK_SIZE_Y;
            int cz = z / ClientConfig.CHUNK_SIZE;

            // Remove from loaded chunks so they get re-requested
            for (int dx = -1; dx <= 1; dx++)
            {
                for (int dy = -1; dy <= 1; dy++)
                {
                    for (int dz = -1; dz <= 1; dz++)
                    {
                        var coord = (cx + dx, cy + dy, cz + dz);
                        loadedChunks.TryRemove(coord, out _);
                        requestedChunks.TryRemove(coord, out _);
                    }
                }
            }
        }

        // Send block break to server via TCP
        public async Task SendBlockBreakAsync(int x, int y, int z)
        {
            if (streamRequest == null || tcpRequest == null || !tcpRequest.Connected)
            {
                Console.WriteLine("[Client] Cannot send block break - not connected");
                return;
            }

            await networkSemaphore.WaitAsync();
            try
            {
                // FIXED: Send only 13 bytes (1 byte type + 12 bytes coordinates)
                byte[] packet = new byte[13];
                packet[0] = (byte)TcpPacketType.BlockBreak;

                BitConverter.TryWriteBytes(packet.AsSpan(1, 4), x);
                BitConverter.TryWriteBytes(packet.AsSpan(5, 4), y);
                BitConverter.TryWriteBytes(packet.AsSpan(9, 4), z);

                await streamRequest.WriteAsync(packet, 0, packet.Length);
                await streamRequest.FlushAsync();

                Console.WriteLine($"[Client] ===== SENT BLOCK BREAK =====");
                Console.WriteLine($"[Client] Position: ({x}, {y}, {z})");
                Console.WriteLine($"[Client] Packet size: 13 bytes");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[Client] Error sending block break: {ex.Message}");
            }
            finally
            {
                networkSemaphore.Release();
            }
        }
        private void HandleUdpPacket(byte[] data)
        {
            if (data.Length < 1) return;

            byte packetType = data[0];

            switch (packetType)
            {
                case 3: // EntityUpdate - other player positions
                    HandleRemotePlayerUpdate(data);
                    break;

                case 4: // KeepAlive
                    _ = udp?.SendAsync(data, data.Length, serverUdpEndpoint);
                    break;

                case 5: // PositionAck - server correction
                    HandleServerPositionUpdate(data);
                    break;

                // REMOVED case 6 (BlockBreak) - now handled via TCP

                default:
                    Console.WriteLine($"[Client] Unknown UDP packet type: {packetType}");
                    break;
            }
        }



        private void HandleServerPositionUpdate(byte[] data)
        {
            if (data.Length < 37) return;

            var update = new ServerPlayerUpdate
            {
                AcknowledgedSequence = BitConverter.ToUInt32(data, 1),
                Position = new Vector3(
                    BitConverter.ToSingle(data, 5),
                    BitConverter.ToSingle(data, 9),
                    BitConverter.ToSingle(data, 13)
                ),
                Velocity = new Vector3(
                    BitConverter.ToSingle(data, 17),
                    BitConverter.ToSingle(data, 21),
                    BitConverter.ToSingle(data, 25)
                ),
                Yaw = BitConverter.ToSingle(data, 29),
                Pitch = BitConverter.ToSingle(data, 33),
                Timestamp = DateTime.UtcNow.Ticks
            };

            game?.NetworkController?.OnServerUpdate(update);
        }

        private void HandleRemotePlayerUpdate(byte[] data)
        {
            if (data.Length < 38) return;

            uint playerIdHash = BitConverter.ToUInt32(data, 1);
            string playerId = playerIdHash.ToString("X8");

            var update = new ServerPlayerUpdate
            {
                Position = new Vector3(
                    BitConverter.ToSingle(data, 5),
                    BitConverter.ToSingle(data, 9),
                    BitConverter.ToSingle(data, 13)
                ),
                Velocity = new Vector3(
                    BitConverter.ToSingle(data, 17),
                    BitConverter.ToSingle(data, 21),
                    BitConverter.ToSingle(data, 25)
                ),
                Yaw = BitConverter.ToSingle(data, 29),
                Pitch = BitConverter.ToSingle(data, 33),
                Timestamp = DateTime.UtcNow.Ticks
            };

            game?.NetworkController?.OnRemotePlayerUpdate(playerId, update);
        }

        public async Task SendUdpAsync(byte[] packet)
        {
            if (udp != null && serverUdpEndpoint != null)
            {
                try
                {
                    await udp.SendAsync(packet, packet.Length);
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[Client] UDP send error: {ex.Message}");
                }
            }
        }

        private async Task ListenForUdpAsync(CancellationToken token)
        {
            if (udp == null) return;
            while (!token.IsCancellationRequested)
            {
                try
                {
                    var result = await udp.ReceiveAsync(token);
                    HandleUdpPacket(result.Buffer);
                }
                catch (OperationCanceledException)
                {
                    break;
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[Client] UDP recv error: {ex}");
                }
            }
        }

        private async Task ChunkUpdateLoopAsync(CancellationToken token)
        {
            await Task.Delay(500, token);
            Console.WriteLine("[Client] Chunk update loop starting...");

            while (!token.IsCancellationRequested)
            {
                try
                {
                    await Task.Delay(UpdateInterval, token);
                    CheckAndUpdateChunks();
                }
                catch (OperationCanceledException) { break; }
                catch (Exception ex)
                {
                    Console.WriteLine($"[Client] Update loop error: {ex.Message}");
                }
            }
        }

        public void UpdateLoadedChunks(Vector3 playerChunk, int renderDistance)
        {
            currentRenderDistance = renderDistance;
            lastPlayerChunk = playerChunk;
        }

        private void CheckAndUpdateChunks()
        {
            if (lastPlayerChunk.X == float.MinValue || game == null)
                return;

            int playerCx = (int)lastPlayerChunk.X;
            int playerCy = (int)lastPlayerChunk.Y;
            int playerCz = (int)lastPlayerChunk.Z;
            int playerBlockY = (int)(game.GetPlayerPosition().Y);

            var toRequest = new List<(int cx, int cy, int cz, float priority)>();

            int rd = currentRenderDistance;
            for (int dx = -rd; dx <= rd; dx++)
            {
                for (int dz = -rd; dz <= rd; dz++)
                {
                    float horizontalDist = MathF.Sqrt(dx * dx + dz * dz);
                    if (horizontalDist > rd)
                        continue;

                    for (int dy = -2; dy <= 2; dy++)
                    {
                        int cx = playerCx + dx;
                        int cy = playerCy + dy;
                        int cz = playerCz + dz;

                        int chunkCenterY = cy * ClientConfig.CHUNK_SIZE_Y + ClientConfig.CHUNK_SIZE_Y / 2;
                        int yDistance = Math.Abs(chunkCenterY - playerBlockY);

                        if (yDistance > 150)
                            continue;

                        var key = (cx, cy, cz);

                        if (!loadedChunks.ContainsKey(key) && !requestedChunks.ContainsKey(key))
                        {
                            float distance = MathF.Sqrt(dx * dx + dy * dy * 4 + dz * dz);

                            if (Math.Abs(dx) <= 1 && Math.Abs(dz) <= 1 && dy <= 0)
                            {
                                distance *= 0.01f;
                            }

                            toRequest.Add((cx, cy, cz, distance));
                        }
                    }
                }
            }

            if (toRequest.Count > 0)
            {
                toRequest.Sort((a, b) => a.priority.CompareTo(b.priority));

                int batchSize = ChunksPerUpdateBatch;
                int toEnqueue = Math.Min(toRequest.Count, batchSize);

                if (requestQueue.Count > MaxPendingUploads)
                    return;

                for (int i = 0; i < toEnqueue; i++)
                {
                    var chunk = toRequest[i];
                    requestedChunks[(chunk.cx, chunk.cy, chunk.cz)] = 0;
                    requestQueue.Enqueue(chunk);
                }
            }

            if (Random.Shared.NextDouble() < 0.1)
            {
                UnloadDistantChunks(playerCx, playerCy, playerCz);
            }
        }

        private void UnloadDistantChunks(int playerCx, int playerCy, int playerCz)
        {
            var toUnload = new List<(int, int, int)>();
            int unloadDist = currentRenderDistance + 2;

            foreach (var coord in loadedChunks.Keys)
            {
                int dx = coord.Item1 - playerCx;
                int dy = coord.Item2 - playerCy;
                int dz = coord.Item3 - playerCz;

                float dist = MathF.Sqrt(dx * dx + dz * dz);

                if (dist > unloadDist || Math.Abs(dy) > 3)
                {
                    toUnload.Add(coord);
                }
            }

            int unloadLimit = Math.Min(toUnload.Count, 4);
            for (int i = 0; i < unloadLimit; i++)
            {
                var coord = toUnload[i];
                if (loadedChunks.TryRemove(coord, out _))
                {
                    requestedChunks.TryRemove(coord, out _);
                }
            }
        }

        private async Task ChunkLoaderLoopAsync(CancellationToken token)
        {
            var activeTasks = new List<Task>();

            while (!token.IsCancellationRequested)
            {
                try
                {
                    activeTasks.RemoveAll(t => t.IsCompleted);

                    while (activeTasks.Count < MaxConcurrentLoads && requestQueue.TryDequeue(out var chunk))
                    {
                        activeTasks.Add(LoadChunkAsync(chunk, token));
                    }

                    if (activeTasks.Count == 0)
                    {
                        await Task.Delay(10, token);
                    }
                    else
                    {
                        await Task.WhenAny(activeTasks);
                    }
                }
                catch (OperationCanceledException) { break; }
                catch (Exception ex)
                {
                    Console.WriteLine($"[Client] Loader error: {ex.Message}");
                    await Task.Delay(100, token);
                }
            }

            await Task.WhenAll(activeTasks);
        }

        private async Task LoadChunkAsync((int cx, int cy, int cz, float priority) chunk, CancellationToken token)
        {
            try
            {
                var (renderMesh, collisionMesh) = await RequestChunkMeshAsync(chunk.cx, chunk.cy, chunk.cz, token);

                loadedChunks[(chunk.cx, chunk.cy, chunk.cz)] = new Aetheris.Chunk();
                game?.Renderer.EnqueueMeshForChunk(chunk.cx, chunk.cy, chunk.cz, renderMesh);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[Client] Error loading ({chunk.cx},{chunk.cy},{chunk.cz}): {ex.Message}");
                requestedChunks.TryRemove((chunk.cx, chunk.cy, chunk.cz), out _);
            }
        }

        private async Task<(float[] renderMesh, CollisionMesh collisionMesh)> RequestChunkMeshAsync(
     int cx, int cy, int cz, CancellationToken token)
        {
            const int MAX_RETRIES = 3;
            int retryCount = 0;

            while (retryCount < MAX_RETRIES)
            {
                if (streamRequest == null || tcpRequest == null || !tcpRequest.Connected)
                {
                    await connectionSemaphore.WaitAsync(token);
                    try
                    {
                        if (streamRequest == null || tcpRequest == null || !tcpRequest.Connected)
                        {
                            await ConnectToServerAsync("127.0.0.1", ClientConfig.SERVER_PORT);
                        }
                    }
                    finally
                    {
                        connectionSemaphore.Release();
                    }
                }

                await networkSemaphore.WaitAsync(token);
                try
                {
                    await SendChunkRequestAsync(cx, cy, cz, token);
                    float[] renderMesh = await ReceiveRenderMeshAsync(token);
                    CollisionMesh collisionMesh = await ReceiveCollisionMeshAsync(token);

                    // Success!
                    return (renderMesh, collisionMesh);
                }
                catch (Exception ex) when (ex.Message.Contains("Invalid payload length") ||
                                           ex.Message.Contains("corruption") ||
                                           ex.Message.Contains("mismatch"))
                {
                    retryCount++;
                    Console.WriteLine($"[Client] Chunk request failed (attempt {retryCount}/{MAX_RETRIES}): {ex.Message}");

                    if (retryCount < MAX_RETRIES)
                    {
                        // Try to recover the TCP stream
                        networkSemaphore.Release(); // Release before recovery attempt

                        bool recovered = await TryRecoverTcpStreamAsync(token);
                        if (!recovered)
                        {
                            throw new Exception($"Failed to recover TCP stream after {retryCount} attempts", ex);
                        }

                        await Task.Delay(100 * retryCount, token); // Exponential backoff
                        continue;
                    }

                    throw;
                }
                finally
                {
                    if (networkSemaphore.CurrentCount == 0)
                        networkSemaphore.Release();
                }
            }

            throw new Exception($"Failed to request chunk ({cx},{cy},{cz}) after {MAX_RETRIES} attempts");
        }
        private async Task SendChunkRequestAsync(int cx, int cy, int cz, CancellationToken token)
        {
            var req = new byte[13];
            req[0] = (byte)TcpPacketType.ChunkRequest;
            BitConverter.TryWriteBytes(req.AsSpan(1, 4), cx);
            BitConverter.TryWriteBytes(req.AsSpan(5, 4), cy);
            BitConverter.TryWriteBytes(req.AsSpan(9, 4), cz);

            await streamRequest!.WriteAsync(req, token);
            await streamRequest.FlushAsync(token);
        }

        private async Task<float[]> ReceiveRenderMeshAsync(CancellationToken token)
        {
            var lenBuf = new byte[4];
            await ReadFullAsync(streamRequest!, lenBuf, 0, 4, token);
            int payloadLen = BitConverter.ToInt32(lenBuf, 0);

            // ENHANCED VALIDATION
            if (payloadLen < 0)
            {
                Console.WriteLine($"[Client] ERROR: Negative payload length {payloadLen} - TCP stream corrupted!");
                Console.WriteLine($"[Client] This usually means broadcast packets leaked into request stream");
                throw new Exception($"TCP stream corruption detected - negative length: {payloadLen}");
            }

            if (payloadLen > 50_000_000) // 50MB safety limit
            {
                Console.WriteLine($"[Client] ERROR: Payload length {payloadLen} exceeds safety limit");
                throw new Exception($"Invalid payload length: {payloadLen} (too large - possible corruption)");
            }

            if (payloadLen == 0)
                return Array.Empty<float>();

            var payload = new byte[payloadLen];
            await ReadFullAsync(streamRequest!, payload, 0, payloadLen, token);

            // Validate payload structure before parsing
            if (payloadLen < 4)
            {
                throw new Exception($"Payload too small: {payloadLen} bytes (need at least 4 for vertex count)");
            }

            int vertexCount = BitConverter.ToInt32(payload, 0);
            const int floatsPerVertex = 7;
            int expectedFloatCount = vertexCount * floatsPerVertex;
            int expectedByteCount = 4 + (expectedFloatCount * sizeof(float)); // +4 for vertex count header

            if (payloadLen != expectedByteCount)
            {
                Console.WriteLine($"[Client] WARNING: Payload size mismatch!");
                Console.WriteLine($"  Expected: {expectedByteCount} bytes ({vertexCount} vertices)");
                Console.WriteLine($"  Received: {payloadLen} bytes");
                throw new Exception($"Payload size mismatch: expected {expectedByteCount}, got {payloadLen}");
            }

            int floatsCount = vertexCount * floatsPerVertex;
            var floats = new float[floatsCount];
            Buffer.BlockCopy(payload, 4, floats, 0, floatsCount * sizeof(float));

            return floats;
        }

        private async Task<bool> TryRecoverTcpStreamAsync(CancellationToken token)
        {
            Console.WriteLine("[Client] Attempting TCP stream recovery...");

            try
            {
                // Close corrupted connections
                streamRequest?.Dispose();
                streamBroadcast?.Dispose();
                tcpRequest?.Close();
                tcpBroadcast?.Close();

                // Wait a moment
                await Task.Delay(100, token);

                // Reconnect
                await ConnectToServerAsync("127.0.0.1", ClientConfig.SERVER_PORT);

                Console.WriteLine("[Client] TCP stream recovery successful");
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[Client] TCP recovery failed: {ex.Message}");
                return false;
            }
        }

        private async Task<CollisionMesh> ReceiveCollisionMeshAsync(CancellationToken token)
        {
            var lenBuf = new byte[4];
            await ReadFullAsync(streamRequest!, lenBuf, 0, 4, token);
            int payloadLen = BitConverter.ToInt32(lenBuf, 0);

            if (payloadLen < 0 || payloadLen > 100_000_000)
                throw new Exception($"Invalid collision payload length: {payloadLen}");

            if (payloadLen == 0)
            {
                return new CollisionMesh
                {
                    Vertices = new List<Vector3>(),
                    Indices = new List<int>()
                };
            }

            var payload = new byte[payloadLen];
            await ReadFullAsync(streamRequest!, payload, 0, payloadLen, token);

            int offset = 0;
            int vertexCount = BitConverter.ToInt32(payload, offset);
            offset += sizeof(int);
            int indexCount = BitConverter.ToInt32(payload, offset);
            offset += sizeof(int);

            var vertices = new List<Vector3>(vertexCount);
            for (int i = 0; i < vertexCount; i++)
            {
                float x = BitConverter.ToSingle(payload, offset);
                offset += sizeof(float);
                float y = BitConverter.ToSingle(payload, offset);
                offset += sizeof(float);
                float z = BitConverter.ToSingle(payload, offset);
                offset += sizeof(float);
                vertices.Add(new Vector3(x, y, z));
            }

            var indices = new List<int>(indexCount);
            for (int i = 0; i < indexCount; i++)
            {
                indices.Add(BitConverter.ToInt32(payload, offset));
                offset += sizeof(int);
            }

            return new CollisionMesh { Vertices = vertices, Indices = indices };
        }
        private static async Task ReadFullAsync(NetworkStream stream, byte[] buf, int off, int count, CancellationToken token)
        {
            int read = 0;
            while (read < count)
            {
                int r = await stream.ReadAsync(buf.AsMemory(off + read, count - read), token);
                if (r <= 0)
                    throw new Exception("Stream closed unexpectedly");
                read += r;
            }
        }

        private void Cleanup()
        {
            Console.WriteLine("[Client] Shutting down...");
            cts?.Cancel();

            loaderTask?.Wait(TimeSpan.FromSeconds(2));
            updateTask?.Wait(TimeSpan.FromSeconds(1));
            tcpListenerTask?.Wait(TimeSpan.FromSeconds(1));

            streamRequest?.Dispose();
            streamBroadcast?.Dispose();
            tcpRequest?.Close();
            tcpBroadcast?.Close();
            udp?.Close();
            networkSemaphore?.Dispose();
            connectionSemaphore?.Dispose();
            cts?.Dispose();
        }
    }
}
