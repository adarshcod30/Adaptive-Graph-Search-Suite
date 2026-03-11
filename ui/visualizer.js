const CV = document.getElementById('cv');
const ctx = CV.getContext('2d');
const mapSelect = document.getElementById('mapSelect');
const algSel = document.getElementById('alg');
const sourceInput = document.getElementById('source');
const targetInput = document.getElementById('target');
const runBackendBtn = document.getElementById('runBackend');
const playBtn = document.getElementById('play');
const pauseBtn = document.getElementById('pause');
const stepBtn = document.getElementById('step');
const speed = document.getElementById('speed');
const speedVal = document.getElementById('speedVal');
const logEl = document.getElementById('log');

const mAlg = document.getElementById('m-alg');
const mTimeC = document.getElementById('m-time-c');
const mSpaceC = document.getElementById('m-space-c');
const mExecTime = document.getElementById('m-exec-time');

speed.oninput = () => speedVal.textContent = speed.value;

let graph = { nodes: [], edges: [] };
let frames = [];
let frameIdx = 0;
let timer = null;

// Camera state for pan/zoom
let camera = { x: 0, y: 0, zoom: 1 };
let isDragging = false;
let dragStart = { x: 0, y: 0 };

function resizeCanvas() {
    CV.width = CV.parentElement.clientWidth;
    CV.height = CV.parentElement.clientHeight;
    centerCamera();
    draw();
}
window.addEventListener('resize', resizeCanvas);

function log(s) {
    const time = new Date().toLocaleTimeString();
    logEl.textContent = `[${time}] ${s}\n${logEl.textContent}`;
}

function clearFrames() {
    frames = [];
    frameIdx = 0;
    mAlg.textContent = '--';
    mTimeC.textContent = '--';
    mSpaceC.textContent = '--';
    mExecTime.textContent = '--';
}

async function fetchMaps() {
    try {
        const res = await fetch('/maps');
        const data = await res.json();
        mapSelect.innerHTML = '';
        data.maps.forEach(m => {
            const opt = document.createElement('option');
            opt.value = m;
            opt.textContent = m;
            mapSelect.appendChild(opt);
        });
        log(`Loaded ${data.maps.length} maps arrays.`);
    } catch(e) { log("Error fetching maps: " + e); }
}

function centerCamera() {
    if(!graph.nodes || graph.nodes.length === 0) return;
    let minx=1e9, miny=1e9, maxx=-1e9, maxy=-1e9;
    graph.nodes.forEach(n => {
        minx = Math.min(minx, n.x); miny = Math.min(miny, n.y);
        maxx = Math.max(maxx, n.x); maxy = Math.max(maxy, n.y);
    });
    
    const pW = maxx - minx;
    const pH = maxy - miny;
    
    const pad = 60;
    const scaleX = (CV.width - pad*2) / (pW || 1);
    const scaleY = (CV.height - pad*2) / (pH || 1);
    camera.zoom = Math.min(scaleX, scaleY);
    
    const cx = minx + pW/2;
    const cy = miny + pH/2;
    
    camera.x = CV.width/2 - cx * camera.zoom;
    camera.y = CV.height/2 - cy * camera.zoom;
}

// Map world coordinates to canvas
const tx = (x) => x * camera.zoom + camera.x;
const ty = (y) => y * camera.zoom + camera.y;

function draw() {
    ctx.clearRect(0,0,CV.width,CV.height);
    if(!graph.nodes || graph.nodes.length === 0) return;
    
    // Draw edges
    ctx.lineWidth = 1 * Math.max(0.5, camera.zoom/20);
    ctx.strokeStyle = 'rgba(255,255,255,0.15)';
    ctx.beginPath();
    graph.edges.forEach(e => {
        const a = graph.nodes[e.u], b = graph.nodes[e.v];
        if(!a || !b) return;
        ctx.moveTo(tx(a.x), ty(a.y));
        ctx.lineTo(tx(b.x), ty(b.y));
    });
    ctx.stroke();

    const nodeSize = Math.max(2, 6 * Math.min(1, camera.zoom/20));
    const showText = camera.zoom > 15;

    // Base nodes
    graph.nodes.forEach(n => {
        const cx = tx(n.x), cy = ty(n.y);
        ctx.beginPath();
        ctx.fillStyle = 'rgba(255,255,255,0.2)';
        ctx.arc(cx, cy, nodeSize, 0, Math.PI*2);
        ctx.fill();
        
        if(showText) {
            ctx.fillStyle = 'rgba(255,255,255,0.5)';
            ctx.font = '10px Inter';
            ctx.fillText(n.id, cx + nodeSize + 4, cy + 3);
        }
    });

    if(frames.length > 0) {
        const f = frames[Math.max(0, Math.min(frameIdx, frames.length-1))];
        
        // Explored
        if(f.explored && f.explored.length > 0) {
            ctx.fillStyle = 'rgba(88, 166, 255, 0.4)'; // Accent blue weak
            f.explored.forEach(id => {
                const n = graph.nodes[id]; if(!n) return;
                ctx.beginPath(); ctx.arc(tx(n.x), ty(n.y), nodeSize * 1.5, 0, Math.PI*2); ctx.fill();
            });
        }
        
        // Frontier
        if(f.frontier && f.frontier.length > 0) {
            ctx.fillStyle = 'rgba(248, 81, 73, 0.8)'; // Red active
            f.frontier.forEach(id => {
                const n = graph.nodes[id]; if(!n) return;
                ctx.beginPath(); ctx.arc(tx(n.x), ty(n.y), nodeSize * 2, 0, Math.PI*2); ctx.fill();
            });
        }
        
        // Path
        if(f.path && f.path.length > 0) {
            ctx.strokeStyle = '#2ea043'; // Success green
            ctx.lineWidth = Math.max(3, camera.zoom/5);
            ctx.beginPath();
            f.path.forEach((id, i) => {
                const n = graph.nodes[id]; if(!n) return;
                if(i===0) ctx.moveTo(tx(n.x), ty(n.y)); else ctx.lineTo(tx(n.x), ty(n.y));
            });
            ctx.stroke();
            
            // Path nodes
            ctx.fillStyle = '#2ea043';
            f.path.forEach(id => {
                const n = graph.nodes[id]; if(!n) return;
                ctx.beginPath(); ctx.arc(tx(n.x), ty(n.y), nodeSize * 2, 0, Math.PI*2); ctx.fill();
            });
        }
    }
}

function play() {
    if(timer) return;
    if(frameIdx >= frames.length - 1) frameIdx = 0;
    timer = setInterval(() => {
        frameIdx++;
        if(frameIdx >= frames.length) { clearInterval(timer); timer = null; }
        draw();
    }, parseInt(speed.value, 10));
}

function pause() { if(timer) { clearInterval(timer); timer = null; } }
function step() { if(frames.length===0) return; pause(); frameIdx = Math.min(frames.length-1, frameIdx+1); draw(); }

async function runOnBackend() {
    pause();
    const alg = algSel.value;
    const mId = mapSelect.value;
    const s = Number(sourceInput.value);
    const t = Number(targetInput.value);

    const payload = { alg: alg, map: mId, source: s, target: t };
    log(`Executing C++ Engine (${alg}) on route ${s} -> ${t} [Map: ${mId}]...`);
    
    try {
        const res = await fetch('/run', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(payload)
        });
        
        if(!res.ok) { log(`Backend error: ${res.status}`); return; }
        
        const obj = await res.json();
        if(obj.trace && obj.trace.graph) {
            graph = obj.trace.graph;
            frames = obj.trace.frames || [];
            
            const meta = obj.trace.metadata || {};
            mAlg.textContent = meta.algorithm || '--';
            mTimeC.textContent = meta.timeComplexity || '--';
            mSpaceC.textContent = meta.spaceComplexity || '--';
            mExecTime.textContent = (meta.executionTimeMs ? meta.executionTimeMs.toFixed(3) : '0.000') + ' ms';
            
            log(`Success! Frames generated: ${frames.length}`);
            clearFrames();
            resizeCanvas();
            draw();
            play(); // Auto-play
        } else {
            log("Engine Error:\n" + String(obj.stdout) + "\n" + String(obj.stderr));
        }
    } catch(e) {
        log("Connection error: " + e);
    }
}

// Mouse panning & zooming
CV.addEventListener('mousedown', e => { isDragging = true; dragStart = { x: e.offsetX, y: e.offsetY }; });
CV.addEventListener('mousemove', e => {
    if(!isDragging) return;
    camera.x += (e.offsetX - dragStart.x);
    camera.y += (e.offsetY - dragStart.y);
    dragStart = { x: e.offsetX, y: e.offsetY };
    draw();
});
CV.addEventListener('mouseup', () => isDragging = false);
CV.addEventListener('mouseleave', () => isDragging = false);
CV.addEventListener('wheel', e => {
    e.preventDefault();
    const zoomIntensity = 0.1;
    const wheel = e.deltaY < 0 ? 1 : -1;
    const zoomFactor = Math.exp(wheel * zoomIntensity);
    
    const mouseX = e.offsetX;
    const mouseY = e.offsetY;
    
    // Zoom toward mouse pointer
    camera.x = mouseX - (mouseX - camera.x) * zoomFactor;
    camera.y = mouseY - (mouseY - camera.y) * zoomFactor;
    camera.zoom *= zoomFactor;
    
    draw();
});

// Bindings
runBackendBtn.onclick = runOnBackend;
playBtn.onclick = play;
pauseBtn.onclick = pause;
stepBtn.onclick = step;

// Init
fetchMaps();
setTimeout(resizeCanvas, 100);
