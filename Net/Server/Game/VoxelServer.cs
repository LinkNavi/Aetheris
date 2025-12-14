// Net/Server/Server_VoxelIntegration.cs - Server-side voxel cube management
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

namespace Aetheris.Server
{
    public partial class Server
    {
        private VoxelCubeManager cubeManager;
        
        private void InitializeVoxelSystem()
        {
            cubeManager = new VoxelCubeManager();
            Console.WriteLine("[Server] Voxel cube system initialized");
        }
        
        private async Task HandleBlockPlaceTcpAsync(TcpClient client, byte[] data)
        {
            try
            {
                if (data.Length < 14)
                {
                    Console.WriteLine($"[Server] Invalid block place packet: {data.Length} bytes");
                    return;
                }
                
                int x = BitConverter.ToInt32(data, 2);
                int y = BitConverter.ToInt32(data, 6);
                int z = BitConverter.ToInt32(data, 10);
                byte blockType = data[13];
                
                Console.WriteLine($"[Server] Block place: ({x},{y},{z}) type={blockType}");
                
                // Place voxel cube (not marching cubes terrain modification)
                if (cubeManager.TryPlaceCube(x, y, z, blockType))
                {
                    Console.WriteLine($"[Server] Placed voxel cube at ({x},{y},{z})");
                    
                    // Broadcast to all clients
                    byte[] broadcast = new byte[14];
                    broadcast[0] = 0x05; // Block place broadcast opcode
                    broadcast[1] = 0x00;
                    
                    BitConverter.GetBytes(x).CopyTo(broadcast, 2);
                    BitConverter.GetBytes(y).CopyTo(broadcast, 6);
                    BitConverter.GetBytes(z).CopyTo(broadcast, 10);
                    broadcast[13] = blockType;
                    
                    await BroadcastTcpAsync(broadcast);
                    
                    Console.WriteLine($"[Server] Broadcasted cube placement to {connectedClients.Count} clients");
                }
                else
                {
                    Console.WriteLine($"[Server] Failed to place cube (position occupied)");
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[Server] Error handling block place: {ex.Message}");
            }
        }
        
        private async Task BroadcastTcpAsync(byte[] data)
        {
            var tasks = new List<Task>();
            
            foreach (var clientStream in connectedClients.Values)
            {
                try
                {
                    tasks.Add(clientStream.WriteAsync(data, 0, data.Length));
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[Server] Broadcast error: {ex.Message}");
                }
            }
            
            await Task.WhenAll(tasks);
        }
        
        public VoxelCubeData[] GetCubesForClient(Vector3 playerPosition)
        {
            // Send cubes within render distance
            return cubeManager.GetCubesInRadius(playerPosition, 100f);
        }
    }
}
