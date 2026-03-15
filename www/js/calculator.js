let current = '0';
let previous = '';
let operator = null;
let operationCount = 0;
let history = [];
const maxHistory = 10;

const display = document.getElementById('display');

function updateDisplay() {
    display.textContent = current.length > 15 ? current.substring(0, 15) + '...' : current;
}

function appendNumber(num) {
    if (current === '0' && num !== '.') {
        current = num;
    } else if (current.length < 20) {
        current += num;
    }
    updateDisplay();
}

function appendOperator(op) {
    if (operator !== null && current !== '') {
        calculate();
    }
    previous = current;
    operator = op;
    current = '';
    operationCount++;
    updateStats();
}

function appendDecimal() {
    if (!current.includes('.') && current.length < 20) {
        current += '.';
    }
    updateDisplay();
}

function toggleSign() {
    if (current !== '0' && current !== '') {
        current = current.startsWith('-') ? current.substring(1) : '-' + current;
        updateDisplay();
    }
}

function calculate() {
    if (operator === null || previous === '' || current === '') return;

    let result;
    const prev = parseFloat(previous);
    const curr = parseFloat(current);

    switch (operator) {
        case '+': result = prev + curr; break;
        case '-': result = prev - curr; break;
        case '*': result = prev * curr; break;
        case '/': result = curr !== 0 ? prev / curr : 'Ошибка'; break;
        default: return;
    }

    const calculation = `${previous} ${operator} ${current} = ${result}`;
    addToHistory(calculation);

    current = String(result);
    operator = null;
    previous = '';
    updateDisplay();
}

function clearDisplay() {
    current = '0';
    previous = '';
    operator = null;
    updateDisplay();
}

function addToHistory(calc) {
    history.unshift(calc);
    if (history.length > maxHistory) history.pop();
    updateHistoryDisplay();
}

function updateHistoryDisplay() {
    const list = document.getElementById('history-list');
    list.innerHTML = history.map((item, idx) => 
        `<div class="history-item" onclick="loadFromHistory(${idx})">${item}</div>`
    ).join('');
    document.getElementById('history-count').textContent = history.length;
}

function loadFromHistory(idx) {
    const item = history[idx];
    const result = item.split(' = ')[1];
    current = result;
    updateDisplay();
}

function updateStats() {
    document.getElementById('operations-count').textContent = operationCount;
}

updateDisplay();
