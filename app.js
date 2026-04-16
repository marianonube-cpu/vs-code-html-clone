let editor;
let currentFilePath = '';

require.config({ paths: { 'vs': 'https://cdnjs.cloudflare.com/ajax/libs/monaco-editor/0.44.0/min/vs' } });

require(['vs/editor/editor.main'], function () {
    editor = monaco.editor.create(document.getElementById('editor'), {
        value: '// ¡Bienvenido al clon de VS Code!\n// Selecciona un archivo para empezar.',
        language: 'javascript',
        theme: 'vs-dark',
        automaticLayout: true
    });

    // Evento de guardado (Ctrl+S)
    editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyS, function () {
        saveFile();
    });
});

function postToHost(action, data = {}) {
    if (window.chrome && window.chrome.webview) {
        window.chrome.webview.postMessage({ action, ...data });
    } else {
        console.warn('WebView2 host no detectado. Acción:', action, data);
        if (action === 'ls') {
            updateFileTree([{ name: 'error.txt', type: 'file' }]);
        }
    }
}

// Escuchar mensajes del host C++
if (window.chrome && window.chrome.webview) {
    window.chrome.webview.addEventListener('message', event => {
        const msg = event.data;
        switch (msg.type) {
            case 'ls':
                updateFileTree(msg.data);
                break;
            case 'read':
                editor.setValue(msg.content);
                currentFilePath = msg.path;
                // Intentar detectar lenguaje por extensión
                const ext = msg.path.split('.').pop();
                const model = editor.getModel();
                // Simple map for common extensions
                const langMap = { 'js': 'javascript', 'cpp': 'cpp', 'html': 'html', 'css': 'css', 'py': 'python' };
                monaco.editor.setModelLanguage(model, langMap[ext] || 'plaintext');
                break;
            case 'exec':
                appendTerminal(msg.output);
                break;
            case 'error':
                appendTerminal('ERROR: ' + msg.message);
                break;
        }
    });
}

function updateFileTree(files) {
    const tree = document.getElementById('file-tree');
    tree.innerHTML = '';
    files.forEach(file => {
        const div = document.createElement('div');
        div.className = file.type === 'dir' ? 'dir-item' : 'file-item';
        div.innerText = (file.type === 'dir' ? '📁 ' : '📄 ') + file.name;
        div.onclick = () => {
            if (file.type === 'file') {
                postToHost('read', { path: file.path || file.name });
            } else {
                postToHost('ls', { path: file.path || file.name });
            }
        };
        tree.appendChild(div);
    });
}

function saveFile() {
    if (currentFilePath) {
        const content = editor.getValue();
        postToHost('write', { path: currentFilePath, content: content });
        appendTerminal('Guardando: ' + currentFilePath);
    }
}

function appendTerminal(text) {
    const output = document.getElementById('terminal-output');
    output.innerText += text + '\n';
    output.scrollTop = output.scrollHeight;
}

// Terminal Input
document.getElementById('terminal-input').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
        const cmd = e.target.value;
        appendTerminal('> ' + cmd);
        postToHost('exec', { command: cmd });
        e.target.value = '';
    }
});

// Inicializar listado
window.onload = () => {
    setTimeout(() => postToHost('ls', { path: '.' }), 1000);
};
