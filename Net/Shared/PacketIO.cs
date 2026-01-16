// Net/Shared/PacketIO.cs - Length-prefixed packet I/O
using System;
using System.IO;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;

namespace Aetheris
{
    public static class PacketIO
    {
        private const int MAX_PACKET_SIZE = 50_000_000;

        public static async Task WritePacketAsync(NetworkStream stream, byte[] data)
        {
            var length = BitConverter.GetBytes(data.Length);
            await stream.WriteAsync(length, 0, 4);
            await stream.WriteAsync(data, 0, data.Length);
            await stream.FlushAsync();
        }

        public static async Task WritePacketAsync(NetworkStream stream, byte[] data, CancellationToken token)
        {
            var length = BitConverter.GetBytes(data.Length);
            await stream.WriteAsync(length, 0, 4, token);
            await stream.WriteAsync(data, 0, data.Length, token);
            await stream.FlushAsync(token);
        }

        public static void WritePacket(NetworkStream stream, byte[] data)
        {
            var length = BitConverter.GetBytes(data.Length);
            stream.Write(length, 0, 4);
            stream.Write(data, 0, data.Length);
            stream.Flush();
        }

        public static async Task<byte[]> ReadPacketAsync(NetworkStream stream, CancellationToken token)
        {
            var lenBuf = new byte[4];
            await ReadFullAsync(stream, lenBuf, 0, 4, token);
            int length = BitConverter.ToInt32(lenBuf, 0);

            if (length <= 0 || length > MAX_PACKET_SIZE)
                throw new InvalidDataException($"Invalid packet length: {length}");

            var data = new byte[length];
            await ReadFullAsync(stream, data, 0, length, token);
            return data;
        }

        public static async Task ReadFullAsync(NetworkStream stream, byte[] buf, int offset, int count, CancellationToken token)
        {
            int read = 0;
            while (read < count)
            {
                int r = await stream.ReadAsync(buf, offset + read, count - read, token);
                if (r <= 0)
                    throw new IOException("Connection closed unexpectedly");
                read += r;
            }
        }

        public static void ReadFull(NetworkStream stream, byte[] buf, int offset, int count)
        {
            int read = 0;
            while (read < count)
            {
                int r = stream.Read(buf, offset + read, count - read);
                if (r <= 0)
                    throw new IOException("Connection closed unexpectedly");
                read += r;
            }
        }
    }
}
