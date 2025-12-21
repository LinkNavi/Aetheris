// Net/Shared/PlayerStats.cs - Health, hunger, and player stat management
using System;

namespace Aetheris
{
    public class PlayerStats
    {
        public float Health { get; private set; }
        public float MaxHealth { get; private set; }
        public float Hunger { get; private set; }
        public float MaxHunger { get; private set; }
        public float Armor { get; private set; }
        
        public bool IsDead => Health <= 0;
        public bool IsStarving => Hunger <= 0;
        public float HealthPercent => Health / MaxHealth;
        public float HungerPercent => Hunger / MaxHunger;
        
        public event Action OnDeath;
        public event Action<float> OnDamage;
        public event Action<float> OnHeal;
        
        private float hungerDrainRate = 0.5f;  // Per second
        private float starveDamageRate = 1f;
        private float regenRate = 0.5f;
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
                TakeDamage(starveDamageRate * deltaTime, "starvation");
            }
            // Regen when well-fed
            else if (HungerPercent >= regenHungerThreshold && Health < MaxHealth)
            {
                Health = MathF.Min(MaxHealth, Health + regenRate * deltaTime);
            }
        }
        
        public void TakeDamage(float amount, string source = "unknown")
        {
            if (IsDead) return;
            
            float reduced = amount * (1f - Armor / (Armor + 100f));
            Health = MathF.Max(0, Health - reduced);
            OnDamage?.Invoke(reduced);
            
            if (IsDead)
            {
                Console.WriteLine($"[Stats] Player died from {source}");
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
        
        public void Respawn()
        {
            Health = MaxHealth;
            Hunger = MaxHunger;
        }
        
        public bool UseFood(int itemId, Inventory inventory)
        {
            var def = ItemRegistry.Get(itemId);
            if (def == null || def.HungerRestore <= 0) return false;
            if (!inventory.HasItem(itemId)) return false;
            
            inventory.RemoveItem(itemId, 1);
            RestoreHunger(def.HungerRestore);
            Heal(def.HealthRestore);
            Console.WriteLine($"[Stats] Ate {def.Name}: +{def.HungerRestore} hunger, +{def.HealthRestore} health");
            return true;
        }
    }
}
