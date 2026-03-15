let isRunning = false;
let startTime = 0;
let completedRequests = 0;
let successRequests = 0;
let errorRequests = 0;
let responseTimes = [];
let testInterval = null;
let elapsedInterval = null;

const logContainer = document.getElementById('log-container');

function log(message, type = 'info') {
    const item = document.createElement('div');
    item.className = `log-item ${type}`;
    item.textContent = `[${new Date().toLocaleTimeString('ru-RU')}] ${message}`;
    logContainer.appendChild(item);
    logContainer.scrollTop = logContainer.scrollHeight;

    if (logContainer.children.length > 100) {
        logContainer.removeChild(logContainer.firstChild);
    }
}

function updateStats() {
    const total = parseInt(document.getElementById('total-requests').value);
    document.getElementById('total-count').textContent = total;
    document.getElementById('completed-count').textContent = completedRequests;

    const successPercent = completedRequests > 0 ? Math.round((successRequests / completedRequests) * 100) : 0;
    document.getElementById('success-count').textContent = successPercent;
    document.getElementById('error-count').textContent = errorRequests;

    const elapsed = (Date.now() - startTime) / 1000;
    const rps = elapsed > 0 ? (completedRequests / elapsed).toFixed(2) : 0;
    document.getElementById('rps-count').textContent = rps;

    const progress = Math.round((completedRequests / total) * 100);
    document.getElementById('progress-fill').style.width = progress + '%';
    document.getElementById('progress-text').textContent = progress + '%';

    if (responseTimes.length > 0) {
        const minTime = Math.min(...responseTimes);
        const maxTime = Math.max(...responseTimes);
        const avgTime = Math.round(responseTimes.reduce((a, b) => a + b) / responseTimes.length);

        document.getElementById('min-time').textContent = minTime;
        document.getElementById('max-time').textContent = maxTime;
        document.getElementById('avg-time').textContent = avgTime;
        document.getElementById('throughput').textContent = rps;
    }
}

async function makeRequest(endpoint) {
    const startReq = Date.now();
    try {
        const response = await fetch(endpoint, {
            method: 'GET',
            timeout: 5000
        });
        const duration = Date.now() - startReq;
        responseTimes.push(duration);
        successRequests++;
        log(`✓ 200 OK (${duration}ms)`, 'success');
        return true;
    } catch (error) {
        const duration = Date.now() - startReq;
        errorRequests++;
        log(`✗ Ошибка: ${error.message}`, 'error');
        return false;
    }
}

async function startStressTest() {
    const concurrent = parseInt(document.getElementById('concurrent-requests').value);
    const total = parseInt(document.getElementById('total-requests').value);
    const endpoint = document.getElementById('test-endpoint').value;

    if (isRunning) return;

    isRunning = true;
    completedRequests = 0;
    successRequests = 0;
    errorRequests = 0;
    responseTimes = [];
    startTime = Date.now();

    document.getElementById('start-btn').disabled = true;
    document.getElementById('stop-btn').disabled = false;
    document.getElementById('progress-section').style.display = 'block';
    document.getElementById('status-badge').className = 'status-badge status-running';
    document.getElementById('status-text').textContent = 'Тест запущен...';

    log('Начат стресс-тест сервера', 'info');
    log(`Запросов: ${total}, Параллельность: ${concurrent}`, 'info');
    log(`Эндпоинт: ${endpoint}`, 'info');

    elapsedInterval = setInterval(() => {
        const elapsed = Math.round((Date.now() - startTime) / 1000);
        document.getElementById('elapsed-time').textContent = elapsed + 's';
    }, 1000);

    let requested = 0;

    const runBatch = async () => {
        const batch = [];
        for (let i = 0; i < concurrent && requested < total; i++) {
            batch.push(makeRequest(endpoint));
            requested++;
        }

        await Promise.all(batch);
        completedRequests = requested;
        updateStats();

        if (requested < total && isRunning) {
            setTimeout(runBatch, 100);
        } else {
            finishTest();
        }
    };

    runBatch();
}

function stopStressTest() {
    isRunning = false;
    finishTest();
}

function finishTest() {
    isRunning = false;
    document.getElementById('start-btn').disabled = false;
    document.getElementById('stop-btn').disabled = true;

    clearInterval(elapsedInterval);

    const elapsed = (Date.now() - startTime) / 1000;
    const throughput = (completedRequests / elapsed).toFixed(2);

    const successPercent = completedRequests > 0 ? Math.round((successRequests / completedRequests) * 100) : 0;

    if (errorRequests === 0) {
        document.getElementById('status-badge').className = 'status-badge status-success';
        document.getElementById('status-text').textContent = `Тест завершен успешно!`;
        log(`Все ${completedRequests} запросов выполнены успешно за ${elapsed.toFixed(2)}s`, 'success');
    } else {
        document.getElementById('status-badge').className = 'status-badge status-error';
        document.getElementById('status-text').textContent = `Тест завершен с ошибками`;
        log(`Тест завершен. Успешно: ${successRequests}, Ошибок: ${errorRequests}`, 'warning');
    }

    log(`Пропускная способность: ${throughput} req/s`, 'info');
}

function resetResults() {
    completedRequests = 0;
    successRequests = 0;
    errorRequests = 0;
    responseTimes = [];
    logContainer.innerHTML = '';
    document.getElementById('progress-fill').style.width = '0%';
    document.getElementById('progress-text').textContent = '0%';
    document.getElementById('status-badge').className = 'status-badge status-idle';
    document.getElementById('status-text').textContent = 'Готов к тестированию';
    updateStats();
    log('Результаты сброшены', 'info');
}

// Initial update
document.addEventListener('DOMContentLoaded', () => {
    updateStats();
    log('Стресс-тест готов. Отрегулируйте параметры и нажмите начать', 'info');
});
