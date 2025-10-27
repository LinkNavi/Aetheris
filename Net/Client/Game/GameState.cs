// File: Net/Client/Game/GameState.cs
using System;

namespace Aetheris
{
    /// <summary>
    /// Game state enumeration - defines all possible states
    /// </summary>
    public enum GameStateType
    {
        Playing,        // Normal gameplay - player can move, mine, etc.
        Inventory,      // Inventory menu open - paused gameplay
        Paused,         // Pause menu (future)
        MainMenu,       // Main menu (future)
        Settings        // Settings menu (future)
    }

    /// <summary>
    /// Game state manager - handles transitions between states
    /// Ensures clean enter/exit behavior for each state
    /// </summary>
    public class GameStateManager
    {
        private GameStateType currentState;
        private GameStateType previousState;
        
        public GameStateType CurrentState => currentState;
        public GameStateType PreviousState => previousState;
        
        // Events for state changes
        public event Action<GameStateType, GameStateType>? OnStateChanged; // (from, to)
        public event Action<GameStateType>? OnStateEnter;
        public event Action<GameStateType>? OnStateExit;
        
        public GameStateManager(GameStateType initialState = GameStateType.Playing)
        {
            currentState = initialState;
            previousState = initialState;
            Console.WriteLine($"[GameStateManager] Initialized in state: {currentState}");
        }
        
        /// <summary>
        /// Transition to a new state
        /// </summary>
        public void TransitionTo(GameStateType newState)
        {
            if (currentState == newState)
            {
                Console.WriteLine($"[GameStateManager] Already in state {newState}, ignoring transition");
                return;
            }
            
            GameStateType oldState = currentState;
            
            Console.WriteLine($"[GameStateManager] Transitioning: {oldState} -> {newState}");
            
            // Exit current state
            OnStateExit?.Invoke(oldState);
            
            // Update state
            previousState = oldState;
            currentState = newState;
            
            // Enter new state
            OnStateEnter?.Invoke(newState);
            
            // Notify listeners
            OnStateChanged?.Invoke(oldState, newState);
        }
        
        /// <summary>
        /// Return to previous state (useful for back buttons)
        /// </summary>
        public void ReturnToPrevious()
        {
            TransitionTo(previousState);
        }
        
        /// <summary>
        /// Check if currently in a specific state
        /// </summary>
        public bool IsInState(GameStateType state) => currentState == state;
        
        /// <summary>
        /// Check if currently in any of the specified states
        /// </summary>
        public bool IsInAnyState(params GameStateType[] states)
        {
            foreach (var state in states)
            {
                if (currentState == state) return true;
            }
            return false;
        }
        
        /// <summary>
        /// Check if gameplay is active (not paused by menus)
        /// </summary>
        public bool IsGameplayActive() => currentState == GameStateType.Playing;
        
        /// <summary>
        /// Check if a menu is open
        /// </summary>
        public bool IsMenuOpen() => currentState != GameStateType.Playing;
    }
}
