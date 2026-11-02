# Editor support

Syntax highlighting for `.tekx` files covers:
- settings and blocks;
- headings and labels;
- inline and display mathematics;
- `@references` and `[@citations]`;
- `{macros}`, emphasis, code and comments.

## Vim and Neovim

```bash
mkdir -p ~/.vim/syntax ~/.vim/ftdetect
```

```bash
cp vim/syntax/tekx.vim ~/.vim/syntax/
```

```bash
cp vim/ftdetect/tekx.vim ~/.vim/ftdetect/
```

For Neovim, use `~/.config/nvim/` in place of `~/.vim/`.

## Visual Studio Code

Copy or link the `vscode` directory into your extensions directory, then
restart VS Code:

```bash
ln -s "$PWD/vscode" ~/.vscode/extensions/tekx
```

## While you write

Keep `tekx -w paper.tekx` running in a terminal. It rebuilds in tens of
milliseconds on every save, and any PDF viewer that reloads changed files
shows the new page.
