// Net/Client/Game/UI/ChatSystem.cs - Client-side chat with command support
using System;
using System.Collections.Generic;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;
using OpenTK.Windowing.GraphicsLibraryFramework;
using Aetheris.UI;
namespace Aetheris
{
    public class ChatSystem : IDisposable
    {
        private readonly List<ChatMessage> messages = new();
        private readonly Queue<ChatMessage> pendingMessages = new();
        private string inputText = "";
        private bool isChatOpen = false;

        private int shaderProgram;
        private int vao, vbo;

        // Configuration
        private const int MAX_MESSAGES = 100;
        private const int VISIBLE_MESSAGES = 10;
        private const float MESSAGE_LIFETIME = 10f; // Seconds before fade
        private const float FADE_DURATION = 2f;
        private const float CHAT_WIDTH = 600f;
        private const float CHAT_Y_OFFSET = 200f;
        private const float LINE_HEIGHT = 20f;
private readonly ITextRenderer? textRenderer;
        // Colors
        private static readonly Vector4 SYSTEM_COLOR = new Vector4(0.7f, 0.7f, 0.7f, 1f);
        private static readonly Vector4 PLAYER_COLOR = new Vector4(1f, 1f, 1f, 1f);
        private static readonly Vector4 ERROR_COLOR = new Vector4(1f, 0.3f, 0.3f, 1f);
        private static readonly Vector4 SUCCESS_COLOR = new Vector4(0.3f, 1f, 0.3f, 1f);
        private static readonly Vector4 COMMAND_COLOR = new Vector4(0.5f, 0.8f, 1f, 1f);
public ChatSystem(ITextRenderer? textRenderer = null)
{
    this.textRenderer = textRenderer;
    InitializeShaders();
    InitializeBuffers();
    AddMessage("Chat system initialized. Press T to chat, / for commands.", ChatMessageType.System);
}
        private void InitializeShaders()
        {
            string vertexShader = @"
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec4 aColor;

out vec2 TexCoord;
out vec4 Color;

uniform mat4 projection;

void main()
{
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
    Color = aColor;
}";

            string fragmentShader = @"
#version 330 core
in vec2 TexCoord;
in vec4 Color;

out vec4 FragColor;

void main()
{
    FragColor = Color;
}";

            int vs = CompileShader(ShaderType.VertexShader, vertexShader);
            int fs = CompileShader(ShaderType.FragmentShader, fragmentShader);

            shaderProgram = GL.CreateProgram();
            GL.AttachShader(shaderProgram, vs);
            GL.AttachShader(shaderProgram, fs);
            GL.LinkProgram(shaderProgram);

            GL.DeleteShader(vs);
            GL.DeleteShader(fs);
        }

        private void InitializeBuffers()
        {
            vao = GL.GenVertexArray();
            vbo = GL.GenBuffer();

            GL.BindVertexArray(vao);
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);

            int stride = 8 * sizeof(float);

            GL.VertexAttribPointer(0, 2, VertexAttribPointerType.Float, false, stride, 0);
            GL.EnableVertexAttribArray(0);

            GL.VertexAttribPointer(1, 2, VertexAttribPointerType.Float, false, stride, 2 * sizeof(float));
            GL.EnableVertexAttribArray(1);

            GL.VertexAttribPointer(2, 4, VertexAttribPointerType.Float, false, stride, 4 * sizeof(float));
            GL.EnableVertexAttribArray(2);

            GL.BindVertexArray(0);
        }

        public void Update(KeyboardState keyboard, float deltaTime)
        {
            // Process pending messages
            while (pendingMessages.Count > 0)
            {
                messages.Add(pendingMessages.Dequeue());
                if (messages.Count > MAX_MESSAGES)
                {
                    messages.RemoveAt(0);
                }
            }

            // Update message timers
            foreach (var msg in messages)
            {
                msg.TimeAlive += deltaTime;
            }

            // Toggle chat
            if (keyboard.IsKeyPressed(Keys.T) && !isChatOpen)
            {
                OpenChat();
            }
            else if (keyboard.IsKeyPressed(Keys.Escape) && isChatOpen)
            {
                CloseChat();
            }
        }

        public void HandleTextInput(char character)
        {
            if (!isChatOpen) return;

            if (char.IsControl(character)) return;

            inputText += character;
        }

        public void HandleKeyPress(Keys key)
        {
            if (!isChatOpen) return;

            if (key == Keys.Backspace && inputText.Length > 0)
            {
                inputText = inputText.Substring(0, inputText.Length - 1);
            }
            else if (key == Keys.Enter)
            {
                SendMessage();
            }
        }

        private void OpenChat()
        {
            isChatOpen = true;
            inputText = "";
            Console.WriteLine("[Chat] Opened");
        }

        private void CloseChat()
        {
            isChatOpen = false;
            inputText = "";
            Console.WriteLine("[Chat] Closed");
        }

        private void SendMessage()
        {
            if (string.IsNullOrWhiteSpace(inputText))
            {
                CloseChat();
                return;
            }

            string message = inputText.Trim();

            // Check if it's a command
            if (message.StartsWith("/"))
            {
                ProcessCommand(message);
            }
            else
            {
                // Normal chat message (would send to server)
                AddMessage($"You: {message}", ChatMessageType.Player);
            }

            CloseChat();
        }

        private void ProcessCommand(string commandText)
        {
            string[] parts = commandText.Substring(1).Split(' ', StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length == 0) return;

            string command = parts[0].ToLower();
            string[] args = parts.Length > 1 ? parts[1..] : Array.Empty<string>();

            // Local commands (client-side)
            switch (command)
            {
                case "help":
                    ShowHelp();
                    break;

                case "clear":
                    messages.Clear();
                    AddMessage("Chat cleared.", ChatMessageType.System);
                    break;

                // Server commands would be sent via network
                case "tp":
                case "teleport":
                case "give":
                case "heal":
                case "kill":
                case "time":
                case "weather":
                    AddMessage($"Sending command to server: {commandText}", ChatMessageType.Command);
                    // TODO: Send to server
                    break;

                default:
                    AddMessage($"Unknown command: {command}. Type /help for commands.", ChatMessageType.Error);
                    break;
            }
        }

        private void ShowHelp()
        {
            AddMessage("=== Available Commands ===", ChatMessageType.System);
            AddMessage("/help - Show this help", ChatMessageType.System);
            AddMessage("/clear - Clear chat", ChatMessageType.System);
            AddMessage("/tp <x> <y> <z> - Teleport (Admin)", ChatMessageType.System);
            AddMessage("/give <item> <count> - Give items (Admin)", ChatMessageType.System);
            AddMessage("/heal - Restore health (Admin)", ChatMessageType.System);
            AddMessage("/kill - Suicide", ChatMessageType.System);
            AddMessage("/time <set/add> <value> - Change time (Admin)", ChatMessageType.System);
        }

        public void AddMessage(string text, ChatMessageType type)
        {
            pendingMessages.Enqueue(new ChatMessage
            {
                Text = text,
                Type = type,
                TimeAlive = 0f,
                Color = GetColorForType(type)
            });
        }

        private Vector4 GetColorForType(ChatMessageType type)
        {
            return type switch
            {
                ChatMessageType.System => SYSTEM_COLOR,
                ChatMessageType.Player => PLAYER_COLOR,
                ChatMessageType.Error => ERROR_COLOR,
                ChatMessageType.Success => SUCCESS_COLOR,
                ChatMessageType.Command => COMMAND_COLOR,
                _ => PLAYER_COLOR
            };
        }

        public void Render(Vector2i windowSize)
        {
            bool depthTestEnabled = GL.IsEnabled(EnableCap.DepthTest);
            bool blendEnabled = GL.IsEnabled(EnableCap.Blend);

            GL.Disable(EnableCap.DepthTest);
            GL.Enable(EnableCap.Blend);
            GL.BlendFunc(BlendingFactor.SrcAlpha, BlendingFactor.OneMinusSrcAlpha);

            GL.UseProgram(shaderProgram);

            var projection = Matrix4.CreateOrthographicOffCenter(0, windowSize.X, 0, windowSize.Y, -1, 1);
            int projLoc = GL.GetUniformLocation(shaderProgram, "projection");
            GL.UniformMatrix4(projLoc, false, ref projection);

            GL.BindVertexArray(vao);

            float x = 20f;
            float y = CHAT_Y_OFFSET;

            // Render messages (bottom to top, most recent at bottom)
            int startIndex = Math.Max(0, messages.Count - VISIBLE_MESSAGES);

            for (int i = startIndex; i < messages.Count; i++)
            {
                var msg = messages[i];

                // Calculate alpha based on lifetime
                float alpha = 1f;
                if (!isChatOpen && msg.TimeAlive > MESSAGE_LIFETIME)
                {
                    float fadeTime = msg.TimeAlive - MESSAGE_LIFETIME;
                    alpha = Math.Max(0f, 1f - (fadeTime / FADE_DURATION));
                }

                if (alpha > 0.01f)
                {
                    Vector4 color = msg.Color;
                    color.W *= alpha;

                    // Background for better readability
                    DrawRect(x - 4, y - 2, CHAT_WIDTH, LINE_HEIGHT, new Vector4(0f, 0f, 0f, 0.5f * alpha));

                    // Message text (would use proper text rendering in real implementation)
                    DrawMessagePlaceholder(x, y, msg.Text, color);

                    y += LINE_HEIGHT;
                }
            }

            // Render input box if chat is open
            if (isChatOpen)
            {
                float inputY = CHAT_Y_OFFSET - 30f;

                // Input background
                DrawRect(x - 4, inputY - 2, CHAT_WIDTH, LINE_HEIGHT + 4, new Vector4(0f, 0f, 0f, 0.8f));
                DrawRectOutline(x - 4, inputY - 2, CHAT_WIDTH, LINE_HEIGHT + 4, 2f, new Vector4(1f, 1f, 1f, 1f));

                // Input text
                string displayText = "> " + inputText + "_";
                DrawMessagePlaceholder(x, inputY, displayText, new Vector4(1f, 1f, 1f, 1f));
            }

            GL.BindVertexArray(0);
            GL.UseProgram(0);

            if (depthTestEnabled) GL.Enable(EnableCap.DepthTest);
            if (!blendEnabled) GL.Disable(EnableCap.Blend);
        }

        private void DrawMessagePlaceholder(float x, float y, string text, Vector4 color)
        {
            if (textRenderer != null)
            {
                textRenderer.DrawText(text, new Vector2(x, y), 0.8f, color);
            }
            else
            {
                // Fallback: draw colored line based on text length
                float width = Math.Min(text.Length * 8f, CHAT_WIDTH - 8f);
                DrawRect(x, y + 4, width, 2f, color);
            }
        }

        private void DrawRect(float x, float y, float w, float h, Vector4 color)
        {
            float[] vertices = new float[]
            {
                x,     y,       0, 0,        color.X, color.Y, color.Z, color.W,
                x + w, y,       1, 0,        color.X, color.Y, color.Z, color.W,
                x + w, y + h,   1, 1,        color.X, color.Y, color.Z, color.W,

                x,     y,       0, 0,        color.X, color.Y, color.Z, color.W,
                x + w, y + h,   1, 1,        color.X, color.Y, color.Z, color.W,
                x,     y + h,   0, 1,        color.X, color.Y, color.Z, color.W,
            };

            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), vertices, BufferUsageHint.DynamicDraw);
            GL.DrawArrays(PrimitiveType.Triangles, 0, 6);
        }

        private void DrawRectOutline(float x, float y, float w, float h, float thickness, Vector4 color)
        {
            DrawRect(x, y + h - thickness, w, thickness, color);
            DrawRect(x, y, w, thickness, color);
            DrawRect(x, y, thickness, h, color);
            DrawRect(x + w - thickness, y, thickness, h, color);
        }

        public bool IsChatOpen() => isChatOpen;

        private int CompileShader(ShaderType type, string source)
        {
            int shader = GL.CreateShader(type);
            GL.ShaderSource(shader, source);
            GL.CompileShader(shader);

            GL.GetShader(shader, ShaderParameter.CompileStatus, out int success);
            if (success == 0)
            {
                string info = GL.GetShaderInfoLog(shader);
                Console.WriteLine($"[ChatSystem] Shader compilation failed: {info}");
            }

            return shader;
        }

        public void Dispose()
        {
            GL.DeleteVertexArray(vao);
            GL.DeleteBuffer(vbo);
            GL.DeleteProgram(shaderProgram);
        }
    }

    public class ChatMessage
    {
        public string Text { get; set; } = "";
        public ChatMessageType Type { get; set; }
        public float TimeAlive { get; set; }
        public Vector4 Color { get; set; }
    }

    public enum ChatMessageType
    {
        System,
        Player,
        Error,
        Success,
        Command
    }
}
