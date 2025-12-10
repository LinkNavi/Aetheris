// Net/Client/Game/Player/FallDamageTracker.cs
using System;
using OpenTK.Mathematics;

namespace Aetheris
{
    /// <summary>
    /// Tracks player fall distance and applies damage on landing
    /// </summary>
    public class FallDamageTracker
    {
        private float highestY = 0f;
        private bool wasFalling = false;
        private const float SAFE_FALL_DISTANCE = 3f;  // No damage below this
        private const float DAMAGE_PER_BLOCK = 2f;    // Damage per block fallen
        
        public void Update(Player player, PlayerStats stats)
        {
            bool isGrounded = player.IsGrounded;
            float currentY = player.Position.Y;
            
            // Track highest point while in air
            if (!isGrounded)
            {
                if (!wasFalling)
                {
                    // Just started falling
                    highestY = currentY;
                    wasFalling = true;
                }
                else
                {
                    // Update highest point
                    highestY = Math.Max(highestY, currentY);
                }
            }
            else if (wasFalling)
            {
                // Just landed - calculate fall damage
                float fallDistance = highestY - currentY;
                
                if (fallDistance > SAFE_FALL_DISTANCE)
                {
                    float damageDistance = fallDistance - SAFE_FALL_DISTANCE;
                    float damage = damageDistance * DAMAGE_PER_BLOCK;
                    
                    stats.TakeDamage(damage, DamageType.Fall);
                    Console.WriteLine($"[FallDamage] Fell {fallDistance:F1} blocks, took {damage:F1} damage");
                }
                
                // Reset tracking
                wasFalling = false;
                highestY = 0f;
            }
        }
    }
}
