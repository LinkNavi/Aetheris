// Net/Client/Client.cs - Refactored to use GameLogic with prediction
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using OpenTK.Mathematics;
using Aetheris.GameLogic;

namespace Aetheris
{
    public partial class Client
    {
        // Core systems
        private Game? game;
        private GameWorld? clientWorld;  // NEW: Client's predicted world state
        private BlockPredictionManager? predictionManager;
        
        // Network connections
        private TcpClient? tcpRequest;
        private TcpClient? tcpBroadcast;
        private NetworkStream? streamRequest;
        private NetworkStream? streamBroadcast;
        private UdpClient? udp;
        private IPEndPoint? serverUdpEndpoint;
        
        // Chunk management
        private readonly ConcurrentDictionary<(int, int, int), Aetheris.Chunk> loadedChunks = new();
        private readonly ConcurrentQueue<(int cx, int cy, int cz, float priority)> requestQueue = new();
        private readonly ConcurrentDictionary<(int, int, int), byte> requestedChunks = new();
        private Vector3 lastPlayerChunk = new Vector3(float.MinValue, float.MinValue, float.MinValue);
        
        // Threading
        private CancellationTokenSource? cts;
        private Task? loaderTask;
        private Task? updateTask;
        private Task? tcpListenerTask;
        
        // Synchronization
        private readonly SemaphoreSlim networkSemaphore = new SemaphoreSlim(1, 1);
        private readonly SemaphoreSlim connectionSemaphore = new SemaphoreSlim(1, 1);
        
        // Configuration
        private int currentRenderDistance;
        private readonly int udpPort = ClientConfig.SERVER_PORT + 1;
        
        // Auto-tuned parameters
        public int MaxConcurrentLoads { get; set; } = 16;
        public int ChunksPerUpdateBatch { get; set; } = 128;
        public int UpdatesPerSecond { get; set; } = 20;
        public int MaxPendingUploads { get; set; } = 64;
        private int UpdateInterval => 1000 / UpdatesPerSecond;
        
        // Broadcast listener tracking
        private readonly ConcurrentDictionary<string, NetworkStream> activeClientStreams = new();
        
        public void Run()
        {
            cts = new CancellationTokenSource();
            AutoTuneSettings();
            
            Task.Run(async () => await ConnectToServerAsync(ClientConfig.SERVER_IP, ClientConfig.SERVER_PORT)).Wait();
            
            // Initialize client-side GameWorld for prediction
            clientWorld = new GameWorld(seed: ServerConfig.WORLD_SEED, name: "ClientWorld");
            predictionManager = new BlockPredictionManager(clientWorld);
            
            game = new Game(new Dictionary<(int, int, int), Aetheris.Chunk>(loadedChunks), this, clientWorld);
            
            loaderTask = Task.Run(() => ChunkLoaderLoopAsync(cts.Token));
            updateTask = Task.Run(() => ChunkUpdateLoopAsync(cts.Token));
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
        
        // ============================================================================
        // TCP Broadcast Listener - Receives server-initiated messages
        // ============================================================================
        
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
                        // Read packet type
                        var packetTypeBuf = new byte[1];
                        int bytesRead = await streamBroadcast.ReadAsync(packetTypeBuf, 0, 1, token);
                        
                        if (bytesRead == 0)
                        {
                            Console.WriteLine("[Client] Server closed broadcast connection");
                            break;
                        }
                        
                        PacketType packetType = (PacketType)packetTypeBuf[0];
                        
                        if (packetType == PacketType.BlockModification)
                        {
                            await HandleBlockModificationBroadcastAsync(token);
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
        
        private async Task HandleBlockModificationBroadcastAsync(CancellationToken token)
        {
            try
            {
                // Read full message (minus the packet type which was already read)
                var buf = new byte[39]; // Total message size - 1 byte for type
                await ReadFullAsync(streamBroadcast!, buf, 0, 39, token);
                
                using var ms = new System.IO.MemoryStream(buf);
                using var reader = new System.IO.BinaryReader(ms);
                
                var message = new BlockModificationMessage();
                message.Deserialize(reader);
                
                Console.WriteLine($"[Client] Received broadcast: {message}");
                
                // Reconcile with our prediction
                if (predictionManager != null)
                {
                    predictionManager.ReconcileModification(message);
                }
                
                // Invalidate affected chunks for mesh regeneration
                InvalidateChunksAroundBlock(message.X, message.Y, message.Z);
                
                Console.WriteLine($"[Client] Processed block modification broadcast");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[Client] Error in HandleBlockModificationBroadcastAsync: {ex.Message}");
                throw;
            }
        }
        
        // ============================================================================
        // Block Modification API (Client-side prediction)
        // ============================================================================
        
        /// <summary>
        /// Mine a block with client-side prediction
        /// </summary>
        public async Task MineBlockAsync(int x, int y, int z)
        {
            if (streamRequest == null || tcpRequest == null || !tcpRequest.Connected || predictionManager == null)
            {
                Console.WriteLine("[Client] Cannot mine block - not connected");
                return;
            }
            
            // Create modification message
            var message = new BlockModificationMessage(
                BlockModificationMessage.ModificationType.Mine,
                x, y, z
            );
            
            // Apply prediction locally FIRST
            uint sequence = predictionManager.PredictModification(message);
            
            // Send to server
            await networkSemaphore.WaitAsync();
            try
            {
                byte[] packet = NetworkMessageSerializer.Serialize(message);
                await streamRequest.WriteAsync(packet, 0, packet.Length);
                await streamRequest.FlushAsync();
                
                Console.WriteLine($"[Client] Sent mine request for ({x},{y},{z}) seq={sequence}");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[Client] Error sending mine request: {ex.Message}");
            }
            finally
            {
                networkSemaphore.Release();
            }
        }
        
        /// <summary>
        /// Place a block with client-side prediction
        /// </summary>
        public async Task PlaceBlockAsync(int x, int y, int z, BlockType blockType, byte rotation = 0)
        {
            if (streamRequest == null || tcpRequest == null || !tcpRequest.Connected || predictionManager == null)
            {
                Console.WriteLine("[Client] Cannot place block - not connected");
                return;
            }
            
            // Create modification message
            var message = new BlockModificationMessage(
                BlockModificationMessage.ModificationType.Place,
                x, y, z,
                (byte)blockType,
                rotation
            );
            
            // Apply prediction locally FIRST
            uint sequence = predictionManager.PredictModification(message);
            
            // Send to server
            await networkSemaphore.WaitAsync();
            try
            {
                byte[] packet = NetworkMessageSerializer.Serialize(message);
                await streamRequest.WriteAsync(packet, 0, packet.Length);
                await streamRequest.FlushAsync();
                
                Console.WriteLine($"[Client] Sent place request for {blockType} at ({x},{y},{z}) seq={sequence}");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[Client] Error sending place request: {ex.Message}");
            }
            finally
            {
                networkSemaphore.Release();
            }
        }
        
        // ============================================================================
        // Chunk Invalidation
        // ============================================================================
        
        private void InvalidateChunksAroundBlock(int x, int y, int z)
        {
            int cx = x / ClientConfig.CHUNK_SIZE;
            int cy = y / ClientConfig.CHUNK_SIZE_Y;
            int cz = z / ClientConfig.CHUNK_SIZE;
            
            var chunksToInvalidate = new HashSet<(int, int, int)>();
            
            // Invalidate the chunk and immediate neighbors
            for (int dx = -1; dx <= 1; dx++)
            {
                for (int dy = -1; dy <= 1; dy++)
                {
                    for (int dz = -1; dz <= 1; dz++)
                    {
                        chunksToInvalidate.Add((cx + dx, cy + dy, cz + dz));
                    }
                }
            }
            
            Console.WriteLine($"[Client] Invalidating {chunksToInvalidate.Count} chunks around ({x},{y},{z})");
            
            foreach (var (chunkX, chunkY, chunkZ) in chunksToInvalidate)
            {
                ForceReloadChunk(chunkX, chunkY, chunkZ);
            }
        }
        
        public void ForceReloadChunk(int cx, int cy, int cz)
        {
            var coord = (cx, cy, cz);
            
            Console.WriteLine($"[Client] ForceReloadChunk called for ({cx}, {cy}, {cz})");
            
            // Remove from client-side caches
            bool wasLoaded = loadedChunks.TryRemove(coord, out _);
            bool wasRequested = requestedChunks.TryRemove(coord, out _);
            
            Console.WriteLine($"[Client]   - Was loaded: {wasLoaded}, was requested: {wasRequested}");
            
            // Clear GPU mesh
            game?.Renderer.ClearChunkMesh(cx, cy, cz);
            
            // Re-request chunk
            requestedChunks.TryRemove(coord, out _);
            requestQueue.Enqueue((cx, cy, cz, 0.0f));
            
            Console.WriteLine($"[Client]   - Queued for immediate reload (queue size: {requestQueue.Count})");
        }
        
        // ============================================================================
        // UDP Packet Handling
        // ============================================================================
        
        private void HandleUdpPacket(byte[] data)
        {
            if (data.Length < 1) return;
            
            PacketType packetType = (PacketType)data[0];
            
            switch (packetType)
            {
                case PacketType.EntityUpdate:
                    HandleRemotePlayerUpdate(data);
                    break;
                    
                case PacketType.KeepAlive:
                    _ = udp?.SendAsync(data, data.Length, serverUdpEndpoint);
                    break;
                    
                case PacketType.PositionAck:
                    HandleServerPositionUpdate(data);
                    break;
                    
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
        
        // ============================================================================
        // Chunk Loading (unchanged from original)
        // ============================================================================
        
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
        
        // ============================================================================
        // Chunk Request/Response
        // ============================================================================
        
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
                            await ConnectToServerAsync(ClientConfig.SERVER_IP, ClientConfig.SERVER_PORT);
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
                        networkSemaphore.Release();
                        bool recovered = await TryRecoverTcpStreamAsync(token);
                        if (!recovered)
                        {
                            throw new Exception($"Failed to recover TCP stream after {retryCount} attempts", ex);
                        }
                        await Task.Delay(100 * retryCount, token);
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
            req[0] = (byte)PacketType.ChunkRequest;
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
            
            if (payloadLen < 0)
            {
                Console.WriteLine($"[Client] ERROR: Negative payload length {payloadLen}");
                throw new Exception($"TCP stream corruption detected - negative length: {payloadLen}");
            }
            
            if (payloadLen > 50_000_000)
            {
                Console.WriteLine($"[Client] ERROR: Payload length {payloadLen} exceeds safety limit");
                throw new Exception($"Invalid payload length: {payloadLen}");
            }
            
            if (payloadLen == 0)
                return Array.Empty<float>();
            
            var payload = new byte[payloadLen];
            await ReadFullAsync(streamRequest!, payload, 0, payloadLen, token);
            
            if (payloadLen < 4)
            {
                throw new Exception($"Payload too small: {payloadLen} bytes");
            }
            
            int vertexCount = BitConverter.ToInt32(payload, 0);
            const int floatsPerVertex = 7;
            int expectedFloatCount = vertexCount * floatsPerVertex;
            int expectedByteCount = 4 + (expectedFloatCount * sizeof(float));
            
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
                streamRequest?.Dispose();
                streamBroadcast?.Dispose();
                tcpRequest?.Close();
                tcpBroadcast?.Close();
                
                await Task.Delay(100, token);
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
