// Net/Shared/PlayerStats.cs - Health, hunger, and player stat management
using System;

namespace Aetheris
{
    public enum DamageType
    {
        Unknown,
        Fall,
        Drowning,
        Fire,
        Lava,
        Starvation,
        Combat,
        Explosion
    }

    public class PlayerStats
    {
        public float Health { get; private set; }
        public float MaxHealth { get; set; }  // Made public set for totem system
        public float Hunger { get; private set; }
        public float MaxHunger { get; private set; }
        public float Armor { get; set; }  // Made public set for armor system
        public float MaxArmor { get; set; } = 100f;  // Added for armor system
        public float HealthRegenRate { get; set; } = 0.5f;  // Added for totem system
        
        public bool IsDead => Health <= 0;
        public bool IsStarving => Hunger <= 0;
        public float HealthPercent => Health / MaxHealth;
        public float HungerPercent => Hunger / MaxHunger;
        
        public event Action OnDeath = delegate { };
        public event Action<float> OnDamage = delegate { };
        public event Action<float> OnHeal = delegate { };
        
        private float hungerDrainRate = 0.5f;  // Per second
        private float starveDamageRate = 1f;
        private float regenHungerThreshold = 0.8f;
        
        public PlayerStats(float maxHealth = 100f, float maxHunger = 100f)
        {
            MaxHealth = maxHealth;
            MaxHunger = maxHunger;
            Health = maxHealth;
            Hunger = maxHunger;
            Armor = 0;
        }
        
        public void Update(float deltaTime)
        {
            if (IsDead) return;
            
            // Drain hunger
            Hunger = MathF.Max(0, Hunger - hungerDrainRate * deltaTime);
            
            // Starve damage
            if (IsStarving)
            {
                TakeDamage(starveDamageRate * deltaTime, DamageType.Starvation);
            }
            // Regen when well-fed
            else if (HungerPercent >= regenHungerThreshold && Health < MaxHealth)
            {
                Health = MathF.Min(MaxHealth, Health + HealthRegenRate * deltaTime);
            }
        }
        
        public void TakeDamage(float amount, DamageType damageType = DamageType.Unknown)
        {
            if (IsDead) return;
            
            float reduced = amount * (1f - Armor / (Armor + 100f));
            Health = MathF.Max(0, Health - reduced);
            OnDamage?.Invoke(reduced);
            
            if (IsDead)
            {
                Console.WriteLine($"[Stats] Player died from {damageType}");
                OnDeath?.Invoke();
            }
        }
        
        public void Heal(float amount)
        {
            if (IsDead) return;
            float healed = MathF.Min(amount, MaxHealth - Health);
            Health += healed;
            if (healed > 0) OnHeal?.Invoke(healed);
        }
        
        public void RestoreHunger(float amount)
        {
            Hunger = MathF.Min(MaxHunger, Hunger + amount);
        }
        
        public void SetArmor(float value) => Armor = MathF.Max(0, value);
        
        public void AddMaxHealth(float bonus) { MaxHealth += bonus; Health += bonus; }
        
        public void Reset()
        {
            Health = MaxHealth;
            Hunger = MaxHunger;
        }
        
        public void Respawn()
        {
            Health = MaxHealth;
            Hunger = MaxHunger;
        }
        
        public bool UseFood(int itemId, Inventory inventory)
        {
            var def = ItemRegistry.Get(itemId);
            if (def == null || def.HungerRestore <= 0) return false;
            if (inventory.CountItem(itemId) <= 0) return false;
            
            inventory.RemoveItem(itemId, 1);
            RestoreHunger(def.HungerRestore);
            Heal(def.HealthRestore);
            Console.WriteLine($"[Stats] Ate {def.Name}: +{def.HungerRestore} hunger, +{def.HealthRestore} health");
            return true;
        }
    }
}
