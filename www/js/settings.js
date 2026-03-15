const fieldsMap = {
    basic: ['port', 'threads', 'max_request_size', 'enable_chroot'],
    ssl: ['cert', 'key'],
    cgi: ['cgi_dir', 'cgi_prefix', 'cgi_ext'],
    cors: ['cors_enabled', 'cors_allow_origin', 'cors_allow_methods', 'cors_allow_headers'],
    log: ['log_file', 'chroot_dir', 'user'],
    advanced: ['error_404_page', 'logging_ratelimit', 'logging_time_window']
};

let configCache = {};

async function loadConfig() {
    try {
        const response = await fetch('/cgi-bin/config.cgi?action=read');
        const data = await response.json();
        configCache = data;

        for (const [key, value] of Object.entries(data)) {
            const field = document.getElementById(key);
            if (field) {
                if (field.type === 'checkbox') {
                    field.checked = value === '1' || value === 'true' || value === 'yes';
                } else {
                    field.value = value;
                }
            }
        }

        document.getElementById('config-count').textContent = Object.keys(data).length;
        updateLastUpdate();
        console.log('✓ Config loaded', data);
    } catch (e) {
        console.error('Failed to load config:', e);
        showStatus('basic', 'Ошибка загрузки конфигурации', 'error');
    }
}

async function saveField(key, value) {
    try {
        const formData = new URLSearchParams();
        formData.append('action', 'write');
        formData.append('key', key);
        formData.append('value', value);

        const response = await fetch('/cgi-bin/config.cgi', {
            method: 'POST',
            body: formData
        });

        const data = await response.json();
        configCache[key] = value;
        return data.status === 'ok';
    } catch (e) {
        console.error('Save error:', e);
        return false;
    }
}

async function saveSection(section) {
    const fields = fieldsMap[section];
    const statusEl = document.getElementById(`status-${section}`);

    showStatus(section, 'Сохранение...', 'loading');

    let saved = 0;
    for (const field of fields) {
        const el = document.getElementById(field);
        if (!el) continue;

        let value = el.type === 'checkbox' ? (el.checked ? '1' : '0') : el.value;
        if (value === '') value = '';

        if (await saveField(field, value)) {
            saved++;
        }
    }

    if (saved === fields.length) {
        showStatus(section, `✓ Сохранено ${saved} параметров`, 'success');
        updateLastUpdate();
    } else {
        showStatus(section, `⚠ Сохранено ${saved} из ${fields.length} параметров`, 'success');
    }

    setTimeout(() => {
        statusEl.classList.remove('visible');
    }, 3000);
}

function resetSection(section) {
    const fields = fieldsMap[section];
    for (const field of fields) {
        const el = document.getElementById(field);
        if (el && configCache[field] !== undefined) {
            if (el.type === 'checkbox') {
                el.checked = configCache[field] === '1' || configCache[field] === 'true';
            } else {
                el.value = configCache[field];
            }
        }
    }
}

function showStatus(section, message, type) {
    const el = document.getElementById(`status-${section}`);
    el.textContent = message;
    el.className = `status-message visible ${type}`;
}

function updateLastUpdate() {
    const now = new Date();
    document.getElementById('last-update').textContent = 
        now.toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
}

document.addEventListener('DOMContentLoaded', () => {
    loadConfig();
    setInterval(updateLastUpdate, 1000);
});
