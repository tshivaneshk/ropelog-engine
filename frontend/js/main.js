import { initGalaxy } from './galaxy.js';

document.addEventListener('DOMContentLoaded', () => {
    // Initialize Three.js cosmic UI background
    initGalaxy();

    // Setup Modals
    const modals = document.querySelectorAll('.modal-overlay');
    const closeBtns = document.querySelectorAll('.close-btn');

    document.querySelectorAll('[data-modal]').forEach(trigger => {
        trigger.addEventListener('click', (e) => {
            const modalId = e.currentTarget.getAttribute('data-modal');
            const modal = document.getElementById(modalId);
            if (modal) {
                modal.classList.add('active');
            }
        });
    });

    closeBtns.forEach(btn => {
        btn.addEventListener('click', (e) => {
            const modal = e.target.closest('.modal-overlay');
            if (modal) modal.classList.remove('active');
        });
    });

    modals.forEach(modal => {
        modal.addEventListener('click', (e) => {
            if (e.target === modal) {
                modal.classList.remove('active');
            }
        });
    });

    // Setup Team Popup logic
    const teamTrigger = document.querySelector('.team-trigger');
    const teamPopup = document.querySelector('.team-popup');

    if (teamTrigger && teamPopup) {
        teamTrigger.addEventListener('mouseenter', () => {
            teamPopup.classList.add('active');
        });

        teamTrigger.addEventListener('mouseleave', () => {
            setTimeout(() => {
                if (!teamPopup.matches(':hover')) {
                    teamPopup.classList.remove('active');
                }
            }, 300);
        });

        teamPopup.addEventListener('mouseleave', () => {
            teamPopup.classList.remove('active');
        });
    }
});
