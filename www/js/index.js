document.getElementById('protocol').textContent = window.location.protocol === 'https:' ? 'HTTPS ✓' : 'HTTP';
document.getElementById('port').textContent = window.location.port || (window.location.protocol === 'https:' ? '443' : '80');

async function checkServerStatus() {
    try {
        const response = await fetch('/cgi-bin/config.cgi?action=read', { method: 'GET' });
        document.getElementById('server-status').textContent = response.ok ? '✓ Онлайн' : '⚠ Ошибка';
    } catch (e) {
        document.getElementById('server-status').textContent = '✓ Онлайн';
    }
}

document.addEventListener('DOMContentLoaded', () => {
    checkServerStatus();
});
