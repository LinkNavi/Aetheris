// Net/Shared/NetworkMessages.cs - Unified network message system
using System;
using System.IO;
using OpenTK.Mathematics;

namespace Aetheris
{
    /// <summary>
    /// Network packet types
    /// </summary>
    public enum PacketType : byte
    {
        // TCP Request/Response
        ChunkRequest = 0,
        ChunkResponse = 1,
        InventorySync = 2,

        // TCP Broadcasts (Server -> All Clients)
        BlockModification = 10,

        // UDP Real-time
        PlayerPosition = 20,
        PlayerInput = 21,
        EntityUpdate = 22,
        PositionAck = 23,
        KeepAlive = 24
    }

    /// <summary>
    /// Base interface for all network messages
    /// </summary>
    public interface INetworkMessage
    {
        PacketType Type { get; }
        void Serialize(BinaryWriter writer);
        void Deserialize(BinaryReader reader);
    }

    /// <summary>
    /// Block modification message using grid-based cube system
    /// </summary>
    public class BlockModificationMessage : INetworkMessage
    {
        public PacketType Type => PacketType.BlockModification;

        public enum ModificationType : byte
        {
            Mine = 0,      // Remove grid cell (cube)
            Place = 1,     // Place grid cell (cube)
            Damage = 2     // Damage block (progressive mining)
        }

        public ModificationType Operation { get; set; }
        public int X { get; set; }
        public int Y { get; set; }
        public int Z { get; set; }
        public byte BlockTypeByte { get; set; }  // Only used for Place
        public byte Rotation { get; set; }        // Only used for Place
        public int Damage { get; set; }           // Only used for Damage

        // Client-side prediction
        public uint ClientSequence { get; set; }
        public long ClientTimestamp { get; set; }

        public BlockModificationMessage() { }

        public BlockModificationMessage(ModificationType operation, int x, int y, int z,
            byte blockType = 0, byte rotation = 0, uint sequence = 0)
        {
            Operation = operation;
            X = x;
            Y = y;
            Z = z;
            BlockTypeByte = blockType;
            Rotation = rotation;
            ClientSequence = sequence;
            ClientTimestamp = DateTime.UtcNow.Ticks;
        }

        public void Serialize(BinaryWriter writer)
        {
            writer.Write((byte)Type);
            writer.Write((byte)Operation);
            writer.Write(X);
            writer.Write(Y);
            writer.Write(Z);
            writer.Write(BlockTypeByte);
            writer.Write(Rotation);
            writer.Write(Damage);
            writer.Write(ClientSequence);
            writer.Write(ClientTimestamp);
        }

        public void Deserialize(BinaryReader reader)
        {
            Operation = (ModificationType)reader.ReadByte();
            X = reader.ReadInt32();
            Y = reader.ReadInt32();
            Z = reader.ReadInt32();
            BlockTypeByte = reader.ReadByte();
            Rotation = reader.ReadByte();
            Damage = reader.ReadInt32();
            ClientSequence = reader.ReadUInt32();
            ClientTimestamp = reader.ReadInt64();
        }

        /// <summary>
        /// Apply this modification using the GRID-BASED system for cube-style terrain
        /// </summary>
        public bool ApplyToWorld(Aetheris.GameLogic.GameWorld world)
        {
            switch (Operation)
            {
                case ModificationType.Mine:
                    // GRID-BASED: Remove entire grid cell as a cube
                    BlockGrid.RemoveBlock(X, Y, Z);
                    Console.WriteLine($"[BlockMod] Removed grid cell at world ({X},{Y},{Z})");
                    return true;

                case ModificationType.Place:
                    // GRID-BASED: Place entire grid cell as a cube
                    var blockType = (BlockType)BlockTypeByte;
                    BlockGrid.PlaceBlock(X, Y, Z, blockType);
                    Console.WriteLine($"[BlockMod] Placed {blockType} grid cell at world ({X},{Y},{Z})");
                    return true;

                case ModificationType.Damage:
                    var pos = Aetheris.GameLogic.BlockPos.FromWorld(X, Y, Z);
                    return world.Modifier.DamageBlock(pos, Damage);

                default:
                    return false;
            }
        }
        
        /// <summary>
        /// Get all chunks affected by this modification (for mesh regeneration)
        /// </summary>
        public List<(int cx, int cy, int cz)> GetAffectedChunks(int chunkSizeX = 32, int chunkSizeY = 96, int chunkSizeZ = 32)
        {
            var (gx, gy, gz) = BlockGrid.WorldToGrid(X, Y, Z);
            return BlockGrid.GetAffectedChunks(gx, gy, gz, chunkSizeX, chunkSizeY, chunkSizeZ);
        }

        public override string ToString()
        {
            var (gx, gy, gz) = BlockGrid.WorldToGrid(X, Y, Z);
            return $"BlockMod[{Operation}] at world({X},{Y},{Z}) grid({gx},{gy},{gz}) block={BlockTypeByte} seq={ClientSequence}";
        }
    }

    /// <summary>
    /// Chunk request message
    /// </summary>
    public class ChunkRequestMessage : INetworkMessage
    {
        public PacketType Type => PacketType.ChunkRequest;

        public int ChunkX { get; set; }
        public int ChunkY { get; set; }
        public int ChunkZ { get; set; }

        public void Serialize(BinaryWriter writer)
        {
            writer.Write((byte)Type);
            writer.Write(ChunkX);
            writer.Write(ChunkY);
            writer.Write(ChunkZ);
        }

        public void Deserialize(BinaryReader reader)
        {
            ChunkX = reader.ReadInt32();
            ChunkY = reader.ReadInt32();
            ChunkZ = reader.ReadInt32();
        }
    }

    /// <summary>
    /// Player position update (UDP)
    /// </summary>
    public class PlayerPositionMessage : INetworkMessage
    {
        public PacketType Type => PacketType.PlayerPosition;

        public uint Sequence { get; set; }
        public Vector3 Position { get; set; }
        public Vector3 Velocity { get; set; }
        public float Yaw { get; set; }
        public float Pitch { get; set; }
        public byte InputFlags { get; set; }

        public void Serialize(BinaryWriter writer)
        {
            writer.Write((byte)Type);
            writer.Write(Sequence);
            writer.Write(Position.X);
            writer.Write(Position.Y);
            writer.Write(Position.Z);
            writer.Write(Velocity.X);
            writer.Write(Velocity.Y);
            writer.Write(Velocity.Z);
            writer.Write(Yaw);
            writer.Write(Pitch);
            writer.Write(InputFlags);
        }

        public void Deserialize(BinaryReader reader)
        {
            Sequence = reader.ReadUInt32();
            Position = new Vector3(
                reader.ReadSingle(),
                reader.ReadSingle(),
                reader.ReadSingle()
            );
            Velocity = new Vector3(
                reader.ReadSingle(),
                reader.ReadSingle(),
                reader.ReadSingle()
            );
            Yaw = reader.ReadSingle();
            Pitch = reader.ReadSingle();
            InputFlags = reader.ReadByte();
        }
    }

    /// <summary>
    /// Position acknowledgment from server (UDP)
    /// </summary>
    public class PositionAckMessage : INetworkMessage
    {
        public PacketType Type => PacketType.PositionAck;

        public uint AcknowledgedSequence { get; set; }
        public Vector3 AuthoritativePosition { get; set; }
        public Vector3 AuthoritativeVelocity { get; set; }
        public float Yaw { get; set; }
        public float Pitch { get; set; }

        public void Serialize(BinaryWriter writer)
        {
            writer.Write((byte)Type);
            writer.Write(AcknowledgedSequence);
            writer.Write(AuthoritativePosition.X);
            writer.Write(AuthoritativePosition.Y);
            writer.Write(AuthoritativePosition.Z);
            writer.Write(AuthoritativeVelocity.X);
            writer.Write(AuthoritativeVelocity.Y);
            writer.Write(AuthoritativeVelocity.Z);
            writer.Write(Yaw);
            writer.Write(Pitch);
        }

        public void Deserialize(BinaryReader reader)
        {
            AcknowledgedSequence = reader.ReadUInt32();
            AuthoritativePosition = new Vector3(
                reader.ReadSingle(),
                reader.ReadSingle(),
                reader.ReadSingle()
            );
            AuthoritativeVelocity = new Vector3(
                reader.ReadSingle(),
                reader.ReadSingle(),
                reader.ReadSingle()
            );
            Yaw = reader.ReadSingle();
            Pitch = reader.ReadSingle();
        }
    }

    /// <summary>
    /// Helper class for serializing/deserializing messages
    /// </summary>
    public static class NetworkMessageSerializer
    {
        public static byte[] Serialize(INetworkMessage message)
        {
            using var ms = new MemoryStream();
            using var writer = new BinaryWriter(ms);
            message.Serialize(writer);
            return ms.ToArray();
        }

        public static T Deserialize<T>(byte[] data) where T : INetworkMessage, new()
        {
            using var ms = new MemoryStream(data);
            using var reader = new BinaryReader(ms);

            // Skip packet type byte (already read)
            reader.ReadByte();

            var message = new T();
            message.Deserialize(reader);
            return message;
        }

        public static PacketType ReadPacketType(byte[] data)
        {
            return data.Length > 0 ? (PacketType)data[0] : PacketType.ChunkRequest;
        }
    }

    /// <summary>
    /// Client-side prediction manager for block modifications (grid-based)
    /// </summary>
    public class BlockPredictionManager
    {
        private readonly Aetheris.GameLogic.GameWorld clientWorld;
        private readonly Queue<BlockModificationMessage> pendingModifications = new();
        private uint nextSequence = 0;

        private const int MAX_PENDING = 64;

        public BlockPredictionManager(Aetheris.GameLogic.GameWorld world)
        {
            clientWorld = world;
        }

        /// <summary>
        /// Predict a block modification locally using grid-based system
        /// </summary>
        public uint PredictModification(BlockModificationMessage message)
        {
            message.ClientSequence = nextSequence++;
            message.ClientTimestamp = DateTime.UtcNow.Ticks;

            // Apply modification locally (prediction) using grid system
            bool success = message.ApplyToWorld(clientWorld);

            if (success)
            {
                // Store for reconciliation
                pendingModifications.Enqueue(message);

                // Limit queue size
                while (pendingModifications.Count > MAX_PENDING)
                {
                    pendingModifications.Dequeue();
                }

                Console.WriteLine($"[Prediction] Applied {message} locally");
            }

            return message.ClientSequence;
        }

        /// <summary>
        /// Reconcile with server's authoritative modification
        /// </summary>
        public void ReconcileModification(BlockModificationMessage serverMessage)
        {
            // Remove acknowledged modifications
            while (pendingModifications.Count > 0)
            {
                var pending = pendingModifications.Peek();

                if (pending.ClientSequence <= serverMessage.ClientSequence)
                {
                    pendingModifications.Dequeue();
                }
                else
                {
                    break;
                }
            }

            // Grid-based reconciliation is simpler since cells are binary (air or solid)
            // Just ensure our grid state matches the server
            var (gx, gy, gz) = BlockGrid.WorldToGrid(serverMessage.X, serverMessage.Y, serverMessage.Z);
            var currentCell = BlockGrid.GetCell(gx, gy, gz);
            
            if (serverMessage.Operation == BlockModificationMessage.ModificationType.Mine)
            {
                if (currentCell.State != BlockGrid.CellState.Air)
                {
                    Console.WriteLine($"[Prediction] Correcting mine at grid ({gx},{gy},{gz})");
                    BlockGrid.RemoveBlock(serverMessage.X, serverMessage.Y, serverMessage.Z);
                }
            }
            else if (serverMessage.Operation == BlockModificationMessage.ModificationType.Place)
            {
                var expectedType = (BlockType)serverMessage.BlockTypeByte;
                if (currentCell.State != BlockGrid.CellState.Solid || currentCell.BlockType != expectedType)
                {
                    Console.WriteLine($"[Prediction] Correcting place at grid ({gx},{gy},{gz})");
                    BlockGrid.PlaceBlock(serverMessage.X, serverMessage.Y, serverMessage.Z, expectedType);
                }
            }
        }

        public int PendingCount => pendingModifications.Count;
    }
}
