// Net/Shared/CraftingSystem.cs - Simple crafting system
using System;
using System.Collections.Generic;
using System.Linq;

namespace Aetheris
{
    public class CraftingRecipe
    {
        public int ResultId { get; set; }
        public int ResultCount { get; set; } = 1;
        public Dictionary<int, int> Ingredients { get; set; } = new();
        public string Category { get; set; } = "misc";
        public bool RequiresWorkbench { get; set; } = false;
        
        public CraftingRecipe(int resultId, int resultCount = 1)
        {
            ResultId = resultId;
            ResultCount = resultCount;
        }
        
        public CraftingRecipe WithIngredient(int itemId, int count)
        {
            Ingredients[itemId] = count;
            return this;
        }
        
        public CraftingRecipe InCategory(string category)
        {
            Category = category;
            return this;
        }
        
        public CraftingRecipe NeedsWorkbench()
        {
            RequiresWorkbench = true;
            return this;
        }
    }
    
    public static class CraftingRegistry
    {
        private static readonly List<CraftingRecipe> recipes = new();
        private static bool initialized = false;
        
        public static void Initialize()
        {
            if (initialized) return;
            
            RegisterToolRecipes();
            RegisterWeaponRecipes();
            RegisterBlockRecipes();
            RegisterMaterialRecipes();
            
            initialized = true;
            Console.WriteLine($"[Crafting] Registered {recipes.Count} recipes");
        }
        
        private static void RegisterToolRecipes()
        {
            recipes.Add(new CraftingRecipe(50, 1).WithIngredient(7, 3).InCategory("tools")); // Wooden Pickaxe
            recipes.Add(new CraftingRecipe(54, 1).WithIngredient(7, 3).InCategory("tools")); // Wooden Axe
            recipes.Add(new CraftingRecipe(55, 1).WithIngredient(7, 2).InCategory("tools")); // Wooden Shovel
            
            recipes.Add(new CraftingRecipe(51, 1).WithIngredient(1, 3).WithIngredient(7, 2).InCategory("tools")); // Stone Pickaxe
            recipes.Add(new CraftingRecipe(52, 1).WithIngredient(400, 3).WithIngredient(7, 2).InCategory("tools").NeedsWorkbench()); // Iron Pickaxe
            recipes.Add(new CraftingRecipe(53, 1).WithIngredient(401, 3).WithIngredient(7, 2).InCategory("tools").NeedsWorkbench()); // Diamond Pickaxe
        }
        
        private static void RegisterWeaponRecipes()
        {
            recipes.Add(new CraftingRecipe(70, 1).WithIngredient(7, 2).InCategory("weapons")); // Wooden Sword
            recipes.Add(new CraftingRecipe(71, 1).WithIngredient(1, 2).WithIngredient(7, 1).InCategory("weapons")); // Stone Sword
            recipes.Add(new CraftingRecipe(72, 1).WithIngredient(400, 2).WithIngredient(7, 1).InCategory("weapons").NeedsWorkbench()); // Iron Sword
            recipes.Add(new CraftingRecipe(73, 1).WithIngredient(401, 2).WithIngredient(7, 1).InCategory("weapons").NeedsWorkbench()); // Diamond Sword
        }
        
        private static void RegisterBlockRecipes()
        {
            recipes.Add(new CraftingRecipe(1, 4).WithIngredient(6, 4).InCategory("blocks")); // Stone from gravel
        }
        
        private static void RegisterMaterialRecipes()
        {
            recipes.Add(new CraftingRecipe(402, 1).WithIngredient(7, 4).InCategory("materials")); // Charcoal from wood
        }
        
        public static IEnumerable<CraftingRecipe> GetAllRecipes() => recipes;
        
        public static IEnumerable<CraftingRecipe> GetRecipesByCategory(string category) =>
            recipes.Where(r => r.Category == category);
        
        public static IEnumerable<CraftingRecipe> GetAvailableRecipes(Inventory inventory, bool hasWorkbench = false)
        {
            foreach (var recipe in recipes)
            {
                if (recipe.RequiresWorkbench && !hasWorkbench) continue;
                if (CanCraft(inventory, recipe)) yield return recipe;
            }
        }
        
        public static bool CanCraft(Inventory inventory, CraftingRecipe recipe)
        {
            foreach (var (itemId, needed) in recipe.Ingredients)
                if (inventory.CountItem(itemId) < needed) return false;
            return true;
        }
        
        public static bool TryCraft(Inventory inventory, CraftingRecipe recipe)
        {
            if (!CanCraft(inventory, recipe)) return false;
            foreach (var (itemId, count) in recipe.Ingredients)
                if (!inventory.RemoveItem(itemId, count)) return false;
            return inventory.AddItem(recipe.ResultId, recipe.ResultCount);
        }
        
        public static CraftingRecipe? FindRecipeForItem(int resultId) =>
            recipes.FirstOrDefault(r => r.ResultId == resultId);
    }
    
    public class CraftingManager
    {
        private readonly Inventory inventory;
        public bool NearWorkbench { get; set; } = false;
        
        public CraftingManager(Inventory inventory)
        {
            this.inventory = inventory;
        }
        
        public List<CraftingRecipe> GetCraftableRecipes() =>
            CraftingRegistry.GetAvailableRecipes(inventory, NearWorkbench).ToList();
        
        public bool Craft(CraftingRecipe recipe)
        {
            if (recipe.RequiresWorkbench && !NearWorkbench) return false;
            return CraftingRegistry.TryCraft(inventory, recipe);
        }
        
        public bool CraftById(int resultId)
        {
            var recipe = CraftingRegistry.FindRecipeForItem(resultId);
            return recipe != null && Craft(recipe);
        }
    }
}
