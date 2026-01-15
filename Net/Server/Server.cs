// Net/Server/Server.cs - Refactored to use GameLogic
using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using System.Numerics;
using System.Collections.Generic;
using Aetheris.GameLogic;

namespace Aetheris
{
    public partial class Server
    {
        // Core systems
        private TcpListener? listener;
        private CancellationTokenSource? cts;
        private readonly ChunkManager chunkManager = new();
        private GameWorld? serverWorld;  // NEW: Server's authoritative world state

        // Mesh cache
        private readonly ConcurrentDictionary<ChunkCoord, (float[] renderMesh, CollisionMesh collisionMesh)> meshCache = new();
        private readonly ConcurrentDictionary<ChunkCoord, SemaphoreSlim> generationLocks = new();
        private const int MaxCachedMeshes = 20000;
        private int cacheSize = 0;

        // Network
        private readonly ConcurrentDictionary<string, NetworkStream> broadcastStreams = new();
        private UdpClient? udpServer;
        private readonly int UDP_PORT = ServerConfig.SERVER_PORT + 1;

        // Player state
        private readonly ConcurrentDictionary<string, PlayerState> playerStates = new();

        // Server tick
        private const double TickRate = 60.0;
        private const double TickDuration = 1000.0 / TickRate;
        private long tickCount = 0;

        // Performance tracking
        private long totalRequests = 0;
        private double totalChunkGenTime = 0;
        private double totalMeshGenTime = 0;
        private double totalSendTime = 0;
        private readonly object perfLock = new();

        // Logging
        private StreamWriter? logWriter;
        private readonly object logLock = new();
        private readonly SemaphoreSlim sendSemaphore = new SemaphoreSlim(1, 1);

        private class PlayerState
        {
            public string PlayerId { get; set; } = Guid.NewGuid().ToString();
            public Vector3 Position { get; set; }
            public Vector3 Velocity { get; set; }
            public Vector2 Rotation { get; set; }
            public long LastUpdate { get; set; }
            public IPEndPoint? EndPoint { get; set; }
            public bool IsGrounded { get; set; }
            public uint LastProcessedSequence { get; set; }
            public Vector3 LastValidatedPosition { get; set; }
            public long LastValidationTime { get; set; }

            private const float BASE_MAX_SPEED = 9.5f;
            private const float BHOP_MAX_SPEED = 25f;
            private const float VERTICAL_SPEED_MAX = 60f;

            private Queue<Vector3> recentPositions = new Queue<Vector3>(5);
            private Queue<float> recentSpeeds = new Queue<float>(5);
            private int violationCount = 0;
            private const int MAX_VIOLATIONS = 5;

            public bool ValidatePosition(Vector3 newPosition, float deltaTime)
            {
                if (LastValidatedPosition == Vector3.Zero || deltaTime > 1.0f)
                {
                    LastValidatedPosition = newPosition;
                    LastValidationTime = DateTime.UtcNow.Ticks;
                    recentPositions.Clear();
                    recentSpeeds.Clear();
                    violationCount = 0;
                    return true;
                }

                deltaTime = Math.Clamp(deltaTime, 0.001f, 0.5f);

                Vector3 movement = newPosition - LastValidatedPosition;
                float distance = movement.Length();
                float horizontalDistance = new Vector3(movement.X, 0, movement.Z).Length();
                float verticalDistance = Math.Abs(movement.Y);

                float currentSpeed = distance / deltaTime;
                float horizontalSpeed = horizontalDistance / deltaTime;
                float verticalSpeed = verticalDistance / deltaTime;

                recentSpeeds.Enqueue(horizontalSpeed);
                if (recentSpeeds.Count > 5) recentSpeeds.Dequeue();

                bool isValid = true;
                string reason = "";

                if (verticalSpeed > VERTICAL_SPEED_MAX)
                {
                    isValid = false;
                    reason = $"excessive vertical speed: {verticalSpeed:F2} m/s";
                }
                else if (horizontalSpeed > BHOP_MAX_SPEED)
                {
                    isValid = false;
                    reason = $"excessive horizontal speed: {horizontalSpeed:F2} m/s";
                }

                if (!isValid)
                {
                    violationCount++;

                    if (violationCount >= MAX_VIOLATIONS)
                    {
                        Console.WriteLine($"[AntiCheat] Player {PlayerId} validation failed: {reason}");
                        violationCount = Math.Max(0, violationCount - 2);
                        return false;
                    }
                    else
                    {
                        isValid = true;
                    }
                }
                else
                {
                    if (violationCount > 0) violationCount--;
                }

                LastValidatedPosition = newPosition;
                LastValidationTime = DateTime.UtcNow.Ticks;

                recentPositions.Enqueue(newPosition);
                if (recentPositions.Count > 5) recentPositions.Dequeue();

                return isValid;
            }

            public void ResetValidation()
            {
                recentPositions.Clear();
                recentSpeeds.Clear();
                violationCount = 0;
                LastValidatedPosition = Position;
            }
        }

        public async Task RunServerAsync()
        {
            SetupLogging();

            Log("[[Server]] Initializing world generation...");
            WorldGen.Initialize();
            WorldGen.SetModificationsEnabled(true);

            // Initialize server's authoritative GameWorld
            serverWorld = new GameWorld(seed: ServerConfig.WORLD_SEED, name: "ServerWorld");

            // Wire up event handlers
            serverWorld.OnTerrainModified += OnServerTerrainModified;

            Log("[[Server]] GameWorld initialized with terrain modification events");

            listener = new TcpListener(IPAddress.Any, ServerConfig.SERVER_PORT);
            listener.Start();
            listener.Server.NoDelay = true;
            cts = new CancellationTokenSource();

            udpServer = new UdpClient(UDP_PORT);
            _ = Task.Run(() => HandleUdpLoop(cts.Token));
            Log($"[[Server]] UDP listening on port {UDP_PORT}");

            Log($"[[Server]] Listening on port {ServerConfig.SERVER_PORT} @ {TickRate} TPS");

            _ = Task.Run(() => ServerTickLoop(cts.Token));
            _ = Task.Run(() => CacheCleanupLoop(cts.Token));

            try
            {
                while (!cts.Token.IsCancellationRequested)
                {
                    var client = await listener.AcceptTcpClientAsync();
                    client.NoDelay = true;
                    _ = Task.Run(() => HandleClientAsync(client, cts.Token), cts.Token);
                }
            }
            catch (OperationCanceledException)
            {
                Log("[[Server]] Shutting down...");
            }
            finally
            {
                logWriter?.Close();
                logWriter?.Dispose();
            }
        }

        // ============================================================================
        // Terrain Modification Event Handler
        // ============================================================================

      // In Server.cs, OnServerTerrainModified method
private void OnServerTerrainModified(TerrainModifyResult result)
{
    if (!result.Success) return;

    Log($"[Server] Terrain modified: {result.BlocksRemoved} removed, {result.BlocksAdded} added, affecting {result.AffectedChunks.Length} chunks");

    // CRITICAL: Invalidate affected chunks 
    foreach (var (cx, cy, cz) in result.AffectedChunks)
    {
        var coord = new ChunkCoord(cx, cy, cz);

        // Remove from mesh cache to force regeneration
        if (meshCache.TryRemove(coord, out _))
        {
            Interlocked.Decrement(ref cacheSize);
        }

        // IMPORTANT: Also unload from chunk manager
        chunkManager.UnloadChunk(coord);

        // Clear generation lock
        if (generationLocks.TryRemove(coord, out var lockObj))
        {
            lockObj.Dispose();
        }

        Log($"[Server] Invalidated chunk {coord} for regeneration");
    }
}

        // ============================================================================
        // TCP Client Handler
        // ============================================================================

        private async Task HandleClientAsync(TcpClient client, CancellationToken token)
        {
            using (client)
            {
                var stream = client.GetStream();
                string clientId = client.Client.RemoteEndPoint?.ToString() ?? Guid.NewGuid().ToString();

                Log($"[Server] Client connected: {clientId}");

                // First byte determines stream purpose
                var firstByte = new byte[1];
                int read = await stream.ReadAsync(firstByte, 0, 1, token);

                if (read == 0)
                {
                    Log($"[Server] Client {clientId} disconnected immediately");
                    return;
                }

                // If first byte is 0xFF, this is a broadcast listener
                if (firstByte[0] == 0xFF)
                {
                    Log($"[Server] Client {clientId} registered as broadcast listener");
                    broadcastStreams[clientId] = stream;

                    try
                    {
                        while (!token.IsCancellationRequested && client.Connected)
                        {
                            await Task.Delay(1000, token);
                        }
                    }
                    finally
                    {
                        broadcastStreams.TryRemove(clientId, out _);
                        Log($"[Server] Broadcast listener {clientId} disconnected");
                    }
                    return;
                }

                // Normal request/response stream
                PacketType packetType = (PacketType)firstByte[0];

                try
                {
                    while (!token.IsCancellationRequested && client.Connected)
                    {
                        switch (packetType)
                        {
                            case PacketType.ChunkRequest:
                                await HandleChunkRequestAsync(stream, clientId, token);
                                break;

                            case PacketType.BlockModification:
                                await HandleBlockModificationAsync(stream, clientId, token);
                                break;

                            default:
                                Log($"[Server] Unknown TCP packet type: {packetType} from {clientId}");
                                break;
                        }

                        // Read next packet type
                        int bytesRead = await stream.ReadAsync(firstByte, 0, 1, token);
                        if (bytesRead == 0) break;
                        packetType = (PacketType)firstByte[0];
                    }
                }
                catch (Exception ex) when (!(ex is OperationCanceledException))
                {
                    Log($"[Server] Client error ({clientId}): {ex.Message}");
                }
                finally
                {
                    Log($"[Server] Client disconnected: {clientId}");
                }
            }
        }

        // ============================================================================
        // Block Modification Handler (NEW - Uses GameLogic)
        // ============================================================================

        private async Task HandleBlockModificationAsync(NetworkStream stream, string clientId, CancellationToken token)
        {
            try
            {
                // Read remaining message bytes (39 bytes after packet type)
                var buf = new byte[39];
                await ReadFullAsync(stream, buf, 0, 39, token);

                using var ms = new MemoryStream(buf);
                using var reader = new BinaryReader(ms);

                var message = new BlockModificationMessage();
                message.Deserialize(reader);

                Log($"[Server] Received {message} from {clientId}");

                // Apply modification to server's authoritative world
                bool success = false;
                if (serverWorld != null)
                {
                    success = message.ApplyToWorld(serverWorld);
                }

                if (success)
                {
                    Log($"[Server] Applied modification successfully");

                    // Broadcast to ALL clients (including sender for reconciliation)
                    await BroadcastBlockModification(message);
                }
                else
                {
                    Log($"[Server] Failed to apply modification");
                }
            }
            catch (Exception ex)
            {
                Log($"[Server] Error in HandleBlockModificationAsync: {ex.Message}");
            }
        }

        private async Task BroadcastBlockModification(BlockModificationMessage message)
        {
            byte[] packet = NetworkMessageSerializer.Serialize(message);

            var deadStreams = new List<string>();
            int successCount = 0;

            foreach (var kvp in broadcastStreams)
            {
                try
                {
                    await kvp.Value.WriteAsync(packet, 0, packet.Length);
                    await kvp.Value.FlushAsync();
                    successCount++;
                }
                catch (Exception ex)
                {
                    Log($"[Server] Error broadcasting to {kvp.Key}: {ex.Message}");
                    deadStreams.Add(kvp.Key);
                }
            }

            foreach (var id in deadStreams)
            {
                broadcastStreams.TryRemove(id, out _);
            }

            Log($"[Server] Broadcasted {message} to {successCount}/{broadcastStreams.Count + deadStreams.Count} clients");
        }

        // ============================================================================
        // Chunk Request Handler
        // ============================================================================

        private async Task HandleChunkRequestAsync(NetworkStream stream, string clientId, CancellationToken token)
        {
            var coord = await ReadChunkRequestAsync(stream, token);
            if (!coord.HasValue)
                return;

            _ = Task.Run(async () =>
            {
                var requestSw = Stopwatch.StartNew();
                double chunkTime = 0, meshTime = 0, sendTime = 0;

                try
                {
                    var result = await GetOrGenerateMeshAsync(coord.Value, token);
                    chunkTime = result.chunkGenTime;
                    meshTime = result.meshGenTime;

                    var sendSw = Stopwatch.StartNew();
                    await SendBothMeshesAsync(stream, result.renderMesh, result.collisionMesh, coord.Value, token);
                    sendTime = sendSw.Elapsed.TotalMilliseconds;

                    lock (perfLock)
                    {
                        totalRequests++;
                        totalChunkGenTime += chunkTime;
                        totalMeshGenTime += meshTime;
                        totalSendTime += sendTime;
                    }

                    double totalTime = requestSw.Elapsed.TotalMilliseconds;
                    Log($"[[Timing]] Chunk {coord.Value}: Chunk={chunkTime:F2}ms Mesh={meshTime:F2}ms Send={sendTime:F2}ms Total={totalTime:F2}ms");
                }
                catch (Exception ex)
                {
                    Log($"[[Server]] Error handling chunk {coord.Value}: {ex.Message}");
                }
            }, token);
        }

        private async Task<ChunkCoord?> ReadChunkRequestAsync(NetworkStream stream, CancellationToken token)
        {
            var buf = ArrayPool<byte>.Shared.Rent(12);
            try
            {
                int totalRead = 0;
                while (totalRead < 12)
                {
                    int bytesRead = await stream.ReadAsync(buf, totalRead, 12 - totalRead, token);
                    if (bytesRead == 0)
                        return null;
                    totalRead += bytesRead;
                }

                int cx = BitConverter.ToInt32(buf, 0);
                int cy = BitConverter.ToInt32(buf, 4);
                int cz = BitConverter.ToInt32(buf, 8);

                return new ChunkCoord(cx, cy, cz);
            }
            catch
            {
                return null;
            }
            finally
            {
                ArrayPool<byte>.Shared.Return(buf);
            }
        }

        private async Task<(float[] renderMesh, CollisionMesh collisionMesh, double chunkGenTime, double meshGenTime)>
            GetOrGenerateMeshAsync(ChunkCoord coord, CancellationToken token)
        {
            if (meshCache.TryGetValue(coord, out var cached))
            {
                Log($"[[Cache]] Hit for {coord}");
                return (cached.renderMesh, cached.collisionMesh, 0, 0);
            }

            var lockObj = generationLocks.GetOrAdd(coord, _ => new SemaphoreSlim(1, 1));

            await lockObj.WaitAsync(token);
            try
            {
                if (meshCache.TryGetValue(coord, out cached))
                {
                    Log($"[[Cache]] Hit after lock for {coord}");
                    return (cached.renderMesh, cached.collisionMesh, 0, 0);
                }

                var chunkSw = Stopwatch.StartNew();
                var chunk = await Task.Run(() => chunkManager.GetOrGenerateChunk(coord), token);
                double chunkGenTime = chunkSw.Elapsed.TotalMilliseconds;

                var meshSw = Stopwatch.StartNew();
                var (renderMesh, collisionMesh) = await Task.Run(() =>
                    MarchingCubes.GenerateMeshes(chunk, coord, chunkManager, isoLevel: 0.5f), token);
                double meshGenTime = meshSw.Elapsed.TotalMilliseconds;

                meshCache[coord] = (renderMesh, collisionMesh);
                Interlocked.Increment(ref cacheSize);

                Log($"[[Generation]] {coord}: Render verts={renderMesh.Length / 7}, Collision verts={collisionMesh.Vertices.Count}");

                return (renderMesh, collisionMesh, chunkGenTime, meshGenTime);
            }
            finally
            {
                lockObj.Release();
            }
        }

        private async Task SendBothMeshesAsync(
            NetworkStream stream, float[] renderMesh, CollisionMesh collisionMesh,
            ChunkCoord coord, CancellationToken token)
        {
            int renderVertexCount = renderMesh.Length / 7;
            int renderPayloadSize = sizeof(int) + renderMesh.Length * sizeof(float);

            int collisionVertexCount = collisionMesh.Vertices.Count;
            int collisionIndexCount = collisionMesh.Indices.Count;
            int collisionPayloadSize = sizeof(int) * 2 +
                                       collisionVertexCount * sizeof(float) * 3 +
                                       collisionIndexCount * sizeof(int);

            var renderPayload = ArrayPool<byte>.Shared.Rent(renderPayloadSize);
            var collisionPayload = ArrayPool<byte>.Shared.Rent(collisionPayloadSize);

            try
            {
                // Pack render mesh
                Array.Copy(BitConverter.GetBytes(renderVertexCount), 0, renderPayload, 0, sizeof(int));
                Buffer.BlockCopy(renderMesh, 0, renderPayload, sizeof(int), renderMesh.Length * sizeof(float));

                // Pack collision mesh
                int offset = 0;
                Array.Copy(BitConverter.GetBytes(collisionVertexCount), 0, collisionPayload, offset, sizeof(int));
                offset += sizeof(int);
                Array.Copy(BitConverter.GetBytes(collisionIndexCount), 0, collisionPayload, offset, sizeof(int));
                offset += sizeof(int);

                foreach (var v in collisionMesh.Vertices)
                {
                    Array.Copy(BitConverter.GetBytes(v.X), 0, collisionPayload, offset, sizeof(float));
                    offset += sizeof(float);
                    Array.Copy(BitConverter.GetBytes(v.Y), 0, collisionPayload, offset, sizeof(float));
                    offset += sizeof(float);
                    Array.Copy(BitConverter.GetBytes(v.Z), 0, collisionPayload, offset, sizeof(float));
                    offset += sizeof(float);
                }

                foreach (var idx in collisionMesh.Indices)
                {
                    Array.Copy(BitConverter.GetBytes(idx), 0, collisionPayload, offset, sizeof(int));
                    offset += sizeof(int);
                }

                await sendSemaphore.WaitAsync(token);
                try
                {
                    var renderLenBytes = BitConverter.GetBytes(renderPayloadSize);
                    await stream.WriteAsync(renderLenBytes, 0, renderLenBytes.Length, token);
                    await stream.WriteAsync(renderPayload, 0, renderPayloadSize, token);

                    var collisionLenBytes = BitConverter.GetBytes(collisionPayloadSize);
                    await stream.WriteAsync(collisionLenBytes, 0, collisionLenBytes.Length, token);
                    await stream.WriteAsync(collisionPayload, 0, collisionPayloadSize, token);

                    await stream.FlushAsync(token);
                }
                finally
                {
                    sendSemaphore.Release();
                }
            }
            finally
            {
                ArrayPool<byte>.Shared.Return(renderPayload);
                ArrayPool<byte>.Shared.Return(collisionPayload);
            }
        }

        // ============================================================================
        // UDP Handlers
        // ============================================================================

        private async Task HandleUdpLoop(CancellationToken token)
        {
            if (udpServer == null) return;

            try
            {
                while (!token.IsCancellationRequested)
                {
                    UdpReceiveResult result = await udpServer.ReceiveAsync(token);
                    HandleUdpPacket(result.Buffer, result.RemoteEndPoint);
                }
            }
            catch (OperationCanceledException)
            {
                Log("[[UDP]] Loop cancelled");
            }
            catch (Exception ex)
            {
                Log($"[[UDP]] Error: {ex.Message}");
            }
        }

        private void HandleUdpPacket(byte[] data, IPEndPoint remoteEndPoint)
        {
            if (data.Length < 1) return;

            PacketType packetType = (PacketType)data[0];

            // CRITICAL FIX: Only accept UDP packet types over UDP
            if (packetType != PacketType.PlayerPosition &&
                packetType != PacketType.KeepAlive &&
                packetType != PacketType.EntityUpdate &&
                packetType != PacketType.PositionAck)
            {
                // Silently ignore non-UDP packets (likely TCP bleed)
                return;
            }

            try
            {
                switch (packetType)
                {
                    case PacketType.PlayerPosition:
                        HandlePlayerPositionPacket(data, remoteEndPoint);
                        break;
                    case PacketType.KeepAlive:
                        HandleKeepAlivePacket(data, remoteEndPoint);
                        break;
                    default:
                        // Should never reach here due to check above
                        break;
                }
            }
            catch (Exception ex)
            {
                Log($"[[UDP]] Error handling packet: {ex.Message}");
            }
        }

        private void HandlePlayerPositionPacket(byte[] data, IPEndPoint remoteEndPoint)
        {
            if (data.Length < 38) return;

            string playerKey = remoteEndPoint.ToString();

            uint sequence = BitConverter.ToUInt32(data, 1);
            float x = BitConverter.ToSingle(data, 5);
            float y = BitConverter.ToSingle(data, 9);
            float z = BitConverter.ToSingle(data, 13);
            float velX = BitConverter.ToSingle(data, 17);
            float velY = BitConverter.ToSingle(data, 21);
            float velZ = BitConverter.ToSingle(data, 25);
            float yaw = BitConverter.ToSingle(data, 29);
            float pitch = BitConverter.ToSingle(data, 33);
            byte inputFlags = data[37];

            var state = playerStates.GetOrAdd(playerKey, _ => new PlayerState
            {
                EndPoint = remoteEndPoint,
                PlayerId = Guid.NewGuid().ToString()
            });

            long now = DateTime.UtcNow.Ticks;
            float deltaTime = state.LastUpdate > 0
                ? (float)TimeSpan.FromTicks(now - state.LastUpdate).TotalSeconds
                : 0.016f;

            Vector3 newPosition = new Vector3(x, y, z);

            bool isValid = state.ValidatePosition(newPosition, deltaTime);

            if (isValid)
            {
                state.Position = newPosition;
                state.Velocity = new Vector3(velX, velY, velZ);
                state.Rotation = new Vector2(yaw, pitch);
                state.LastProcessedSequence = sequence;
            }
            else
            {
                newPosition = state.Position;
            }

            state.LastUpdate = now;
            state.EndPoint = remoteEndPoint;

            _ = SendPositionAcknowledgment(state, sequence);
            _ = BroadcastPlayerState(playerKey, state);
        }

        private async Task SendPositionAcknowledgment(PlayerState state, uint sequence)
        {
            if (state.EndPoint == null || udpServer == null) return;

            byte[] packet = new byte[37];
            packet[0] = (byte)PacketType.PositionAck;

            BitConverter.TryWriteBytes(packet.AsSpan(1, 4), sequence);
            BitConverter.TryWriteBytes(packet.AsSpan(5, 4), state.Position.X);
            BitConverter.TryWriteBytes(packet.AsSpan(9, 4), state.Position.Y);
            BitConverter.TryWriteBytes(packet.AsSpan(13, 4), state.Position.Z);
            BitConverter.TryWriteBytes(packet.AsSpan(17, 4), state.Velocity.X);
            BitConverter.TryWriteBytes(packet.AsSpan(21, 4), state.Velocity.Y);
            BitConverter.TryWriteBytes(packet.AsSpan(25, 4), state.Velocity.Z);
            BitConverter.TryWriteBytes(packet.AsSpan(29, 4), state.Rotation.X);
            BitConverter.TryWriteBytes(packet.AsSpan(33, 4), state.Rotation.Y);

            try
            {
                await udpServer.SendAsync(packet, packet.Length, state.EndPoint);
            }
            catch (Exception ex)
            {
                Log($"[UDP] Error sending ack to {state.PlayerId}: {ex.Message}");
            }
        }

        private async Task BroadcastPlayerState(string excludePlayer, PlayerState state)
        {
            byte[] packet = new byte[38];
            packet[0] = (byte)PacketType.EntityUpdate;

            var playerIdBytes = Guid.Parse(state.PlayerId).ToByteArray();
            Array.Copy(playerIdBytes, 0, packet, 1, 4);

            BitConverter.TryWriteBytes(packet.AsSpan(5, 4), state.Position.X);
            BitConverter.TryWriteBytes(packet.AsSpan(9, 4), state.Position.Y);
            BitConverter.TryWriteBytes(packet.AsSpan(13, 4), state.Position.Z);
            BitConverter.TryWriteBytes(packet.AsSpan(17, 4), state.Velocity.X);
            BitConverter.TryWriteBytes(packet.AsSpan(21, 4), state.Velocity.Y);
            BitConverter.TryWriteBytes(packet.AsSpan(25, 4), state.Velocity.Z);
            BitConverter.TryWriteBytes(packet.AsSpan(29, 4), state.Rotation.X);
            BitConverter.TryWriteBytes(packet.AsSpan(33, 4), state.Rotation.Y);
            packet[37] = (byte)(state.IsGrounded ? 1 : 0);

            foreach (var player in playerStates)
            {
                if (player.Key != excludePlayer && player.Value.EndPoint != null)
                {
                    try
                    {
                        await udpServer!.SendAsync(packet, packet.Length, player.Value.EndPoint);
                    }
                    catch (Exception ex)
                    {
                        Log($"[UDP] Error broadcasting to {player.Key}: {ex.Message}");
                    }
                }
            }
        }

        private void HandleKeepAlivePacket(byte[] data, IPEndPoint remoteEndPoint)
        {
            _ = Task.Run(async () =>
            {
                try
                {
                    await udpServer!.SendAsync(data, data.Length, remoteEndPoint);
                }
                catch (Exception ex)
                {
                    Log($"[[UDP]] Error sending keep-alive: {ex.Message}");
                }
            });
        }

        // ============================================================================
        // Server Tick Loop
        // ============================================================================

        private async Task ServerTickLoop(CancellationToken token)
        {
            var sw = Stopwatch.StartNew();
            double accumulator = 0;

            while (!token.IsCancellationRequested)
            {
                double frameTime = sw.Elapsed.TotalMilliseconds;
                sw.Restart();
                accumulator += frameTime;

                while (accumulator >= TickDuration)
                {
                    tickCount++;
                    accumulator -= TickDuration;
                }

                double sleepTime = TickDuration - sw.Elapsed.TotalMilliseconds;
                if (sleepTime > 1)
                {
                    await Task.Delay((int)sleepTime, token);
                }

                if (tickCount % (int)(TickRate * 5) == 0)
                {
                    lock (perfLock)
                    {
                        if (totalRequests > 0)
                        {
                            double avgChunk = totalChunkGenTime / totalRequests;
                            double avgMesh = totalMeshGenTime / totalRequests;
                            double avgSend = totalSendTime / totalRequests;
                            double avgTotal = avgChunk + avgMesh + avgSend;

                            Log($"[[Server]] Tick {tickCount} | Cache: {cacheSize}/{MaxCachedMeshes}");
                        }
                    }
                }
            }
        }

        private async Task CacheCleanupLoop(CancellationToken token)
        {
            while (!token.IsCancellationRequested)
            {
                try
                {
                    await Task.Delay(60000, token);

                    if (cacheSize > MaxCachedMeshes)
                    {
                        var cleanupSw = Stopwatch.StartNew();

                        int toRemove = cacheSize / 4;
                        int removed = 0;

                        foreach (var coord in meshCache.Keys)
                        {
                            if (removed >= toRemove) break;

                            if (meshCache.TryRemove(coord, out _))
                            {
                                removed++;
                                Interlocked.Decrement(ref cacheSize);
                            }
                        }

                        Log($"[[Cache Cleanup]] Removed {removed} meshes in {cleanupSw.Elapsed.TotalMilliseconds:F2}ms, {cacheSize} remaining");
                    }
                }
                catch (OperationCanceledException)
                {
                    break;
                }
                catch (Exception ex)
                {
                    Log($"[[Server]] Cache cleanup error: {ex.Message}");
                }
            }
        }

        // ============================================================================
        // Utility Methods
        // ============================================================================

        private static async Task ReadFullAsync(NetworkStream stream, byte[] buffer, int offset, int count, CancellationToken token)
        {
            int totalRead = 0;
            while (totalRead < count)
            {
                int bytesRead = await stream.ReadAsync(buffer, offset + totalRead, count - totalRead, token);
                if (bytesRead == 0)
                {
                    throw new IOException("Connection closed unexpectedly");
                }
                totalRead += bytesRead;
            }
        }

        private void SetupLogging()
        {
            string logDir = Path.Combine(AppContext.BaseDirectory, "logs");
            Directory.CreateDirectory(logDir);

            string logPath = Path.Combine(logDir, $"server_log_{DateTime.Now:yyyyMMdd_HHmmss}.txt");
            try
            {
                logWriter = new StreamWriter(logPath, append: false) { AutoFlush = true };
                Log($"[[Server]] Log file created: {logPath}");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[ERROR] Failed to create log file: {ex.Message}");
            }
        }

        private void Log(string message)
        {
            string timestamp = DateTime.Now.ToString("HH:mm:ss.fff");
            string logMessage = $"[{timestamp}] {message}";

            lock (logLock)
            {
                try
                {
                    Console.WriteLine(logMessage);
                    logWriter?.WriteLine(logMessage);
                    logWriter?.Flush();
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[ERROR] Failed to write to log file: {ex.Message}");
                }
            }
        }

        public void Stop()
        {
            cts?.Cancel();
            listener?.Stop();
            sendSemaphore?.Dispose();

            foreach (var lockObj in generationLocks.Values)
            {
                lockObj.Dispose();
            }
            generationLocks.Clear();
        }
    }
}
