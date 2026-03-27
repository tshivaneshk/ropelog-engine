import * as THREE from 'three';

export function initGalaxy(isInteractive = true, isStatic = false) {
    const container = document.getElementById('canvas-container');
    if (!container) return;

    const scene = new THREE.Scene();
    scene.fog = new THREE.FogExp2(0x010204, 0.0015);

    const camera = new THREE.PerspectiveCamera(75, window.innerWidth / window.innerHeight, 0.1, 2000);
    // Position for a diagonal look
    camera.position.set(-150, 180, 250);
    camera.lookAt(0, 0, 0);

    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    renderer.setSize(window.innerWidth, window.innerHeight);
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2)); // optimize performance
    container.appendChild(renderer.domElement);

    // Galaxy parameters (Performance optimized)
    const params = {
        count: 70000, // Reduced count by over 50% for performance
        size: 0.012,
        radius: 500,
        branches: 5,
        spin: 1.8,
        randomness: 0.45,
        randomnessPower: 3,
        insideColor: '#f9c5ff', // More vivid purple-pink core
        outsideColor: '#2b00ff'  // Deep blue/purple edge
    };

    let geometry = null;
    let material = null;
    let pointsGroup = new THREE.Group();
    scene.add(pointsGroup);

    // Invisible plane for precise 3D mouse tracking (matches galaxy's orientation)
    const hitPlaneGeo = new THREE.PlaneGeometry(3000, 3000);
    hitPlaneGeo.rotateX(-Math.PI / 2);
    const hitPlaneMat = new THREE.MeshBasicMaterial({ visible: false, depthWrite: false });
    const hitPlane = new THREE.Mesh(hitPlaneGeo, hitPlaneMat);
    pointsGroup.add(hitPlane);

    // Texture for glowing stars
    const createStarTexture = () => {
        const canvas = document.createElement('canvas');
        canvas.width = 32;
        canvas.height = 32;
        const ctx = canvas.getContext('2d');
        const gradient = ctx.createRadialGradient(16, 16, 0, 16, 16, 16);
        gradient.addColorStop(0, 'rgba(255, 255, 255, 1)');
        gradient.addColorStop(0.2, 'rgba(255, 255, 255, 0.8)');
        gradient.addColorStop(0.5, 'rgba(200, 200, 255, 0.2)');
        gradient.addColorStop(1, 'rgba(0, 0, 0, 0)');
        ctx.fillStyle = gradient;
        ctx.fillRect(0, 0, 32, 32);
        return new THREE.CanvasTexture(canvas);
    };

    const starTexture = createStarTexture();

    const generateGalaxy = () => {
        geometry = new THREE.BufferGeometry();
        const positions = new Float32Array(params.count * 3);
        const colors = new Float32Array(params.count * 3);
        const scales = new Float32Array(params.count);

        const colorInside = new THREE.Color(params.insideColor);
        const colorOutside = new THREE.Color(params.outsideColor);

        for (let i = 0; i < params.count; i++) {
            const i3 = i * 3;
            const radius = Math.random() * params.radius;
            const spinAngle = radius * params.spin;
            const branchAngle = (i % params.branches) / params.branches * Math.PI * 2;

            const randomX = Math.pow(Math.random(), params.randomnessPower) * (Math.random() < 0.5 ? 1 : -1) * params.randomness * radius;
            const randomY = Math.pow(Math.random(), params.randomnessPower) * (Math.random() < 0.5 ? 1 : -1) * params.randomness * radius * 0.4; // Flattened Y
            const randomZ = Math.pow(Math.random(), params.randomnessPower) * (Math.random() < 0.5 ? 1 : -1) * params.randomness * radius;

            positions[i3] = Math.cos(branchAngle + spinAngle) * radius + randomX;
            positions[i3 + 1] = randomY;
            positions[i3 + 2] = Math.sin(branchAngle + spinAngle) * radius + randomZ;

            // Color mix
            const mixedColor = colorInside.clone();
            mixedColor.lerp(colorOutside, radius / params.radius);

            // Add subtle random color variation for realism
            if (Math.random() > 0.95) {
                mixedColor.setHex(0x00f5ff); // occasional cyan stars
            }
            if (Math.random() > 0.99) {
                mixedColor.setHex(0xffffff); // bright white stars
            }

            colors[i3] = mixedColor.r;
            colors[i3 + 1] = mixedColor.g;
            colors[i3 + 2] = mixedColor.b;

            scales[i] = Math.random();
        }

        geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
        geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
        geometry.setAttribute('aScale', new THREE.BufferAttribute(scales, 1));

        // Use custom shader material for premium glow effect and scale animation
        material = new THREE.ShaderMaterial({
            depthWrite: false,
            blending: THREE.AdditiveBlending,
            vertexColors: true,
            transparent: true,
            uniforms: {
                uTime: { value: 0 },
                uSize: { value: 16.0 * renderer.getPixelRatio() }, // Reduced base size
                uTexture: { value: starTexture },
                uMouse: { value: new THREE.Vector3(9999, 9999, 9999) }
            },
            vertexShader: `
                uniform float uTime;
                uniform float uSize;
                uniform vec3 uMouse;
                attribute float aScale;
                varying vec3 vColor;
                void main() {
                    vec4 modelPosition = modelMatrix * vec4(position, 1.0);
                    
                    float interaction = 0.0;
                    vec3 diff = abs(modelPosition.xyz - uMouse);
                    
                    // Check bounding limits for massive performance gain before distance calculation
                    if(diff.x < 150.0 && diff.y < 150.0 && diff.z < 150.0) {
                        float dist = length(modelPosition.xyz - uMouse);
                        interaction = smoothstep(150.0, 0.0, dist);
                    }
                    
                    // Subtle galaxy wave
                    modelPosition.y += sin(uTime + modelPosition.x * 0.1) * 2.0;

                    // Pop out particles cleanly on the Y axis
                    modelPosition.y += interaction * 15.0;

                    vec4 viewPosition = viewMatrix * modelPosition;
                    gl_Position = projectionMatrix * viewPosition;
                    
                    // Scale point size cleanly based on mouse focus
                    gl_PointSize = uSize * aScale * (100.0 / -viewPosition.z) * (1.0 + interaction * 1.5);
                    vColor = mix(color, vec3(0.6, 0.9, 1.0), interaction * 0.5); // Add subtle cyan glow to cursor
                }
            `,
            fragmentShader: `
                uniform sampler2D uTexture;
                varying vec3 vColor;
                void main() {
                    vec4 textureColor = texture2D(uTexture, gl_PointCoord);
                    gl_FragColor = vec4(vColor, 1.0) * textureColor;
                }
            `
        });

        const points = new THREE.Points(geometry, material);
        pointsGroup.add(points);

        // Tilt the whole galaxy for dramatic diagonal effect
        pointsGroup.rotation.z = Math.PI / 6;
        pointsGroup.rotation.x = Math.PI / 8;
        pointsGroup.position.y = -20;
    };

    generateGalaxy();

    // Cosmic Dust Layer (Performance optimized)
    const generateDust = () => {
        const dustGeo = new THREE.BufferGeometry();
        const dustCount = 3000; // Reduced from 8000
        const dustPos = new Float32Array(dustCount * 3);
        const dustCol = new Float32Array(dustCount * 3);

        for (let i = 0; i < dustCount; i++) {
            dustPos[i * 3] = (Math.random() - 0.5) * 800;
            dustPos[i * 3 + 1] = (Math.random() - 0.5) * 800;
            dustPos[i * 3 + 2] = (Math.random() - 0.5) * 800;

            dustCol[i * 3] = 0.2;
            dustCol[i * 3 + 1] = 0.1;
            dustCol[i * 3 + 2] = 0.4; // Very faint purple
        }
        dustGeo.setAttribute('position', new THREE.BufferAttribute(dustPos, 3));
        dustGeo.setAttribute('color', new THREE.BufferAttribute(dustCol, 3));

        const dustMat = new THREE.PointsMaterial({
            size: 2,
            sizeAttenuation: true,
            depthWrite: false,
            blending: THREE.AdditiveBlending,
            vertexColors: true,
            transparent: true,
            opacity: 0.1,
            map: starTexture
        });

        const dust = new THREE.Points(dustGeo, dustMat);
        scene.add(dust);
        return dust;
    };

    const dustLayer = generateDust();

    // Shooting Stars
    const shootingStars = [];
    const createShootingStar = () => {
        const geo = new THREE.BufferGeometry();
        // A line with 2 points
        const pos = new Float32Array([0, 0, 0, 0, 0, 0]);
        geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
        const mat = new THREE.LineBasicMaterial({
            color: 0xcceeff,
            transparent: true,
            opacity: 0.8,
            blending: THREE.AdditiveBlending
        });
        const line = new THREE.Line(geo, mat);
        scene.add(line);
        return {
            line,
            reset: function () {
                this.x = (Math.random() - 0.5) * 600;
                this.y = Math.random() * 200 + 100;
                this.z = (Math.random() - 0.5) * 400 - 100;
                this.speed = Math.random() * 8 + 8;
                this.length = Math.random() * 20 + 10;
                this.dx = -this.speed;
                this.dy = -this.speed * 0.5;
                this.dz = this.speed * 0.2;
                this.active = false;
                this.delay = Math.random() * 1000;
                this.age = 0;
            },
            update: function (dt) {
                if (!this.active) {
                    this.delay -= dt;
                    if (this.delay <= 0) this.active = true;
                    return;
                }
                this.x += this.dx;
                this.y += this.dy;
                this.z += this.dz;
                this.age++;

                const positions = this.line.geometry.attributes.position.array;
                positions[0] = this.x;
                positions[1] = this.y;
                positions[2] = this.z;

                positions[3] = this.x - this.dx * this.length * 0.1;
                positions[4] = this.y - this.dy * this.length * 0.1;
                positions[5] = this.z - this.dz * this.length * 0.1;
                this.line.geometry.attributes.position.needsUpdate = true;

                // fade out
                this.line.material.opacity = Math.max(0, 1.0 - (this.age / 50));

                if (this.age > 50) this.reset();
            }
        };
    };

    for (let i = 0; i < 3; i++) {
        const star = createShootingStar();
        star.reset();
        star.delay = Math.random() * 2000; // stagger starts
        shootingStars.push(star);
    }

    // Interaction & Parallax
    let mouseX = 0;
    let mouseY = 0;
    let targetX = 0;
    let targetY = 0;

    const windowHalfX = window.innerWidth / 2;
    const windowHalfY = window.innerHeight / 2;

    // Raycaster for perfect 3D interaction tracking
    const raycaster = new THREE.Raycaster();
    const ndcMouse = new THREE.Vector2(999, 999);

    // Interaction
    if (isInteractive) {
        document.addEventListener('mousemove', (event) => {
            mouseX = event.clientX - windowHalfX;
            mouseY = event.clientY - windowHalfY;

            // Normalized Device Coordinates for Raycaster
            ndcMouse.x = (event.clientX / window.innerWidth) * 2 - 1;
            ndcMouse.y = -(event.clientY / window.innerHeight) * 2 + 1;
        });
    }

    // Handle window resize
    window.addEventListener('resize', () => {
        camera.aspect = window.innerWidth / window.innerHeight;
        camera.updateProjectionMatrix();
        renderer.setSize(window.innerWidth, window.innerHeight);
        if (isStatic) {
            renderer.render(scene, camera);
        }
    });

    const clock = new THREE.Clock();
    let previousTime = 0;

    // Animation Loop
    const tick = () => {
        const elapsedTime = clock.getElapsedTime();
        const deltaTime = (elapsedTime - previousTime) * 1000;
        previousTime = elapsedTime;

        if (material) {
            material.uniforms.uTime.value = elapsedTime;

            if (isInteractive) {
                raycaster.setFromCamera(ndcMouse, camera);
                const intersects = raycaster.intersectObject(hitPlane);

                if (intersects.length > 0) {
                    const intersectPoint = intersects[0].point;
                    material.uniforms.uMouse.value.set(intersectPoint.x, intersectPoint.y, intersectPoint.z);
                } else {
                    material.uniforms.uMouse.value.set(9999, 9999, 9999); // Off-screen reset
                }
            }
        }

        // Rotate galaxy core slightly faster
        pointsGroup.rotation.y = elapsedTime * 0.08;

        // Rotate dust layer slowly
        dustLayer.rotation.y = elapsedTime * 0.02;
        dustLayer.rotation.x = elapsedTime * 0.01;

        // Update shooting stars
        shootingStars.forEach(star => star.update(deltaTime));

        // More intense Camera Intertia based on mouse movement
        targetX = mouseX * 0.15;
        targetY = mouseY * 0.15;

        camera.position.x += (targetX - camera.position.x) * 0.05 - 0.2;
        camera.position.y += (-targetY - camera.position.y) * 0.05;
        camera.lookAt(0, 0, 0);

        renderer.render(scene, camera);
        if (!isStatic) {
            requestAnimationFrame(tick);
        }
    };

    tick();
}
