function createParticles() {
    const container = document.getElementById('particle-container');
    const count = Math.random() * 20 + 10;

    for (let i = 0; i < count; i++) {
        const particle = document.createElement('div');
        particle.className = 'particle';
        particle.innerHTML = '✨';
        particle.style.left = '50%';
        particle.style.top = '50%';
        particle.style.fontSize = (Math.random() * 20 + 10) + 'px';
        particle.style.opacity = 1;

        container.appendChild(particle);

        const angle = (Math.PI * 2 * i) / count;
        const velocity = Math.random() * 3 + 2;
        let x = 0, y = 0;
        let vx = Math.cos(angle) * velocity;
        let vy = Math.sin(angle) * velocity;

        const animate = () => {
            x += vx;
            y += vy;
            particle.style.transform = `translate(${x}px, ${y}px)`;
            particle.style.opacity -= 0.02;

            if (particle.style.opacity > 0) {
                requestAnimationFrame(animate);
            } else {
                particle.remove();
            }
        };

        animate();
    }
}

// Auto-create particles periodically
setInterval(() => {
    if (Math.random() > 0.8) {
        createParticles();
    }
}, 2000);
