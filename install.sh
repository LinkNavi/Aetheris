#!/bin/bash
# Run: chmod +x install-zsh-plugins.sh && ./install-zsh-plugins.sh

ZSH_CUSTOM="${ZSH_CUSTOM:-$HOME/.oh-my-zsh/custom}"

echo "==> Installing Powerlevel10k theme..."
git clone --depth=1 https://github.com/romkatv/powerlevel10k.git "$ZSH_CUSTOM/themes/powerlevel10k" 2>/dev/null || echo "    (already installed)"

echo "==> Installing zsh-autosuggestions..."
git clone --depth=1 https://github.com/zsh-users/zsh-autosuggestions "$ZSH_CUSTOM/plugins/zsh-autosuggestions" 2>/dev/null || echo "    (already installed)"

echo "==> Installing zsh-syntax-highlighting..."
git clone --depth=1 https://github.com/zsh-users/zsh-syntax-highlighting "$ZSH_CUSTOM/plugins/zsh-syntax-highlighting" 2>/dev/null || echo "    (already installed)"

echo "==> Installing zsh-completions..."
git clone --depth=1 https://github.com/zsh-users/zsh-completions "$ZSH_CUSTOM/plugins/zsh-completions" 2>/dev/null || echo "    (already installed)"

echo "==> Installing zsh-history-substring-search..."
git clone --depth=1 https://github.com/zsh-users/zsh-history-substring-search "$ZSH_CUSTOM/plugins/zsh-history-substring-search" 2>/dev/null || echo "    (already installed)"

echo "==> Installing fzf-tab..."
git clone --depth=1 https://github.com/Aloxaf/fzf-tab "$ZSH_CUSTOM/plugins/fzf-tab" 2>/dev/null || echo "    (already installed)"

echo "==> Installing fzf..."
if ! command -v fzf &>/dev/null; then
  git clone --depth 1 https://github.com/junegunn/fzf.git ~/.fzf 2>/dev/null
  ~/.fzf/install --all --no-update-rc
else
  echo "    (already installed)"
fi

echo ""
echo "Done! Now run: exec zsh"
echo "Then run: p10k configure"
