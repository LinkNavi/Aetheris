// Net/Shared/PlayerStats.cs - Shared player statistics
using System;

namespace Aetheris
{
    /// <summary>
    /// Player statistics - synchronized between client and server
    /// </summary>
    public class PlayerStats
    {
        // Health
        public float Health { get; set; } = 100f;
        public float MaxHealth { get; set; } = 100f;
        
        // Armor
        public float Armor { get; set; } = 0f;
        public float MaxArmor { get; set; } = 100f;
        
        // Hunger
        public float Hunger { get; set; } = 100f;
        public float MaxHunger { get; set; } = 100f;
        
        // Regeneration
        public float HealthRegenRate { get; set; } = 1f;  // HP per second when well-fed
        public float HungerDecayRate { get; set; } = 0.1f; // Hunger per second
        
        // Damage modifiers
        public float DamageMultiplier { get; set; } = 1f;
        public float DamageReduction { get; set; } = 0f;
        
        // Status flags
        public bool IsDead => Health <= 0;
        public bool IsStarving => Hunger <= 0;
        public bool IsWellFed => Hunger >= 80f;
        
        public PlayerStats()
        {
            Reset();
        }
        
        public void Reset()
        {
            Health = MaxHealth;
            Armor = 0f;
            Hunger = MaxHunger;
        }
        
        /// <summary>
        /// Apply damage with armor reduction
        /// </summary>
        public float TakeDamage(float amount, DamageType damageType = DamageType.Generic)
        {
            if (IsDead) return 0f;
            
            float finalDamage = amount * DamageMultiplier;
            
            // Apply armor reduction
            if (Armor > 0 && damageType != DamageType.Hunger)
            {
                float armorReduction = Math.Min(Armor / MaxArmor * 0.8f, 0.8f); // Max 80% reduction
                float damageToArmor = finalDamage * 0.5f;
                float damageToHealth = finalDamage * (1f - armorReduction);
                
                Armor = Math.Max(0f, Armor - damageToArmor);
                finalDamage = damageToHealth;
            }
            
            // Apply damage reduction
            finalDamage *= (1f - DamageReduction);
            
            Health = Math.Max(0f, Health - finalDamage);
            
            return finalDamage;
        }
        
        /// <summary>
        /// Heal the player
        /// </summary>
        public float Heal(float amount)
        {
            float oldHealth = Health;
            Health = Math.Min(MaxHealth, Health + amount);
            return Health - oldHealth;
        }
        
        /// <summary>
        /// Add armor points
        /// </summary>
        public void AddArmor(float amount)
        {
            Armor = Math.Min(MaxArmor, Armor + amount);
        }
        
        /// <summary>
        /// Restore hunger
        /// </summary>
        public void Feed(float amount)
        {
            Hunger = Math.Min(MaxHunger, Hunger + amount);
        }
        
        /// <summary>
        /// Update stats (called every tick on server)
        /// </summary>
        public void Update(float deltaTime)
        {
            // Hunger decay
            Hunger = Math.Max(0f, Hunger - HungerDecayRate * deltaTime);
            
            // Regeneration when well-fed
            if (IsWellFed && Health < MaxHealth)
            {
                Heal(HealthRegenRate * deltaTime);
            }
            
            // Starvation damage
            if (IsStarving)
            {
                TakeDamage(2f * deltaTime, DamageType.Hunger);
            }
        }
        
        /// <summary>
        /// Serialize for network transmission
        /// </summary>
        public byte[] Serialize()
        {
            using var ms = new System.IO.MemoryStream();
            using var writer = new System.IO.BinaryWriter(ms);
            
            writer.Write(Health);
            writer.Write(MaxHealth);
            writer.Write(Armor);
            writer.Write(MaxArmor);
            writer.Write(Hunger);
            writer.Write(MaxHunger);
            
            return ms.ToArray();
        }
        
        /// <summary>
        /// Deserialize from network data
        /// </summary>
        public static PlayerStats Deserialize(byte[] data)
        {
            using var ms = new System.IO.MemoryStream(data);
            using var reader = new System.IO.BinaryReader(ms);
            
            return new PlayerStats
            {
                Health = reader.ReadSingle(),
                MaxHealth = reader.ReadSingle(),
                Armor = reader.ReadSingle(),
                MaxArmor = reader.ReadSingle(),
                Hunger = reader.ReadSingle(),
                MaxHunger = reader.ReadSingle()
            };
        }
    }
    
    public enum DamageType
    {
        Generic,
        Fall,
        Hunger,
        Melee,
        Projectile,
        Fire,
        Drowning
    }
}
