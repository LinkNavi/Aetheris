// Net/Client/Client_BlockPlaceHandler.cs - Handle block placement broadcasts
using System;
using System.Threading.Tasks;

namespace Aetheris
{
    public partial class Client
    {
        private async Task HandleBlockPlaceBroadcastAsync(byte[] data)
        {
            try
            {
                if (data.Length < 14)
                {
                    Console.WriteLine($"[Client] Invalid block place broadcast: {data.Length} bytes");
                    return;
                }
                
                int x = BitConverter.ToInt32(data, 2);
                int y = BitConverter.ToInt32(data, 6);
                int z = BitConverter.ToInt32(data, 10);
                byte blockType = data[13];
                
                Console.WriteLine($"[Client] Block place broadcast: ({x},{y},{z}) type={blockType}");
                
                // Add to voxel cube system (not marching cubes)
                game?.OnBlockPlaceBroadcast(x, y, z, blockType);
                
                await Task.CompletedTask;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[Client] Error handling block place: {ex.Message}");
            }
        }
        
        public async Task SendBlockPlaceAsync(int x, int y, int z, byte blockType)
        {
            try
            {
                byte[] packet = new byte[14];
                packet[0] = 0x05; // Block place opcode
                packet[1] = 0x00;
                
                BitConverter.GetBytes(x).CopyTo(packet, 2);
                BitConverter.GetBytes(y).CopyTo(packet, 6);
                BitConverter.GetBytes(z).CopyTo(packet, 10);
                packet[13] = blockType;
                
                await tcpStream.WriteAsync(packet, 0, packet.Length);
                
                Console.WriteLine($"[Client] Sent block place: ({x},{y},{z}) type={blockType}");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[Client] Error sending block place: {ex.Message}");
            }
        }
    }
}
