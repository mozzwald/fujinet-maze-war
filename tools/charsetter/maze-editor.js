(function () {
    'use strict';
    const Model = globalThis.MazeWarLynx;
    const EDITOR_SCALE = 40;
    const TILED_SCALE = 8;     // 3x3 neighbour preview
    const UNDO_LIMIT = 60;

    const ids = [
        'tileset-input', 'load-tileset', 'save-tileset', 'undo', 'redo',
        'dirty-indicator', 'palette', 'active-pen-label', 'tiled-canvas',
        'actual-canvas', 'sprite-cost', 'entry-name', 'entry-binding',
        'editor-canvas', 'empty-state', 'library-filter', 'library-kind',
        'tile-list', 'player-preview', 'animation-canvas', 'animation-label', 'animation-play',
    ];
    const el = {};
    ids.forEach(id => { el[id.replace(/-([a-z])/g, (_, c) => c.toUpperCase())] = document.getElementById(id); });

    let tileset = null;
    let fileName = 'maze-war-tiles.json';
    let previewPlayer = 0;
    let animationPlaying = true;
    let animationFrame = 0;
    let strokeStarted = false;
    let selectedKey = null;
    let activePen = 1;
    let dirty = false;
    let painting = false;
    const undoStack = [];
    const redoStack = [];

    function selected() { return tileset && selectedKey ? Model.findEntry(tileset, selectedKey) : null; }

    function setDirty(value) {
        dirty = value;
        el.dirtyIndicator.hidden = !value;
    }

    function snapshot() {
        if (!tileset) return;
        undoStack.push(Model.cloneState(tileset));
        if (undoStack.length > UNDO_LIMIT) undoStack.shift();
        redoStack.length = 0;
        updateUndoButtons();
    }

    function updateUndoButtons() {
        el.undo.disabled = !undoStack.length;
        el.redo.disabled = !redoStack.length;
    }

    function stepHistory(from, to) {
        if (!tileset || !from.length) return;
        to.push(Model.cloneState(tileset));
        Model.restoreState(tileset, from.pop());
        updateUndoButtons();
        setDirty(true);
        renderAll();
    }

    // ---------------------------------------------------------------- render

    function palette() { return tileset ? Model.palette(tileset, selected()?.entry, previewPlayer) : []; }

    function drawEntry(ctx, entry, x, y, scale) {
        for (let py = 0; py < entry.height; py++) {
            for (let px = 0; px < entry.width; px++) {
                ctx.fillStyle = Model.colour(tileset, entry, px, py, previewPlayer) ||
                    ((px + py) % 2 ? '#30343a' : '#444a52');
                ctx.fillRect(x + px * scale, y + py * scale, scale, scale);
            }
        }
    }

    function renderPalette() {
        el.palette.replaceChildren();
        palette().forEach((colour, index) => {
            const button = document.createElement('button');
            button.className = 'swatch' + (index === activePen ? ' active' : '');
            button.style.background = colour || 'repeating-conic-gradient(#444 0% 25%,#222 0% 50%) 0/12px 12px';
            button.title = `Pen ${index} — ${colour}${index === 0 && selected()?.kind !== 'terrain' ? ' (transparent)' : ''}`;
            button.addEventListener('click', () => { activePen = index; renderPalette(); });
            el.palette.appendChild(button);
        });
        el.activePenLabel.textContent = `$${activePen.toString(16).toUpperCase()}`;
    }

    function renderEditor() {
        const item = selected();
        el.emptyState.hidden = Boolean(item);
        el.editorCanvas.style.visibility = item ? 'visible' : 'hidden';
        if (!item) return;
        const {width, height} = item.entry;
        el.editorCanvas.width = width * EDITOR_SCALE;
        el.editorCanvas.height = height * EDITOR_SCALE;
        const ctx = el.editorCanvas.getContext('2d');
        drawEntry(ctx, item.entry, 0, 0, EDITOR_SCALE);
        ctx.strokeStyle = 'rgba(255,255,255,0.25)';
        for (let x=0; x<=width; x++) {
            ctx.beginPath(); ctx.moveTo(x*EDITOR_SCALE+0.5,0);
            ctx.lineTo(x*EDITOR_SCALE+0.5,height*EDITOR_SCALE); ctx.stroke();
        }
        for (let y=0; y<=height; y++) {
            ctx.beginPath(); ctx.moveTo(0,y*EDITOR_SCALE+0.5);
            ctx.lineTo(width*EDITOR_SCALE,y*EDITOR_SCALE+0.5); ctx.stroke();
        }
    }

    function renderPreviews() {
        const item = selected();
        const tiled = el.tiledCanvas.getContext('2d');
        const actual = el.actualCanvas.getContext('2d');
        tiled.fillStyle = '#121416';
        tiled.fillRect(0, 0, el.tiledCanvas.width, el.tiledCanvas.height);
        actual.clearRect(0, 0, el.actualCanvas.width, el.actualCanvas.height);
        if (!item) { el.spriteCost.textContent = ''; return; }
        el.tiledCanvas.width = item.entry.width * TILED_SCALE * 3;
        el.tiledCanvas.height = item.entry.height * TILED_SCALE * 3;
        el.actualCanvas.width = item.entry.width;
        el.actualCanvas.height = item.entry.height;
        el.actualCanvas.style.width = `${item.entry.width * 8}px`;
        el.actualCanvas.style.height = `${item.entry.height * 8}px`;
        for (let ty = 0; ty < 3; ty++) {
            for (let tx = 0; tx < 3; tx++) {
                drawEntry(tiled, item.entry, tx * item.entry.width * TILED_SCALE, ty * item.entry.height * TILED_SCALE, TILED_SCALE);
            }
        }
        drawEntry(actual, item.entry, 0, 0, 1);
        el.spriteCost.textContent = `${Model.spriteBytes(item.entry).length} bytes on cart`;
    }

    function renderHeader() {
        const item = selected();
        el.entryName.textContent = item ? item.label : 'No tile selected';
        el.entryBinding.textContent = item ? item.detail : '';
    }

    function renderLibrary() {
        if (!tileset) { el.tileList.replaceChildren(); return; }
        const filter = el.libraryFilter.value.trim().toLowerCase();
        const kind = el.libraryKind.value;

        el.tileList.replaceChildren();
        Model.entries(tileset).forEach(item => {
            if (kind !== 'all' && item.kind !== kind) return;

            if (filter && !item.label.toLowerCase().includes(filter) && !item.detail.includes(filter)) return;
            const button = document.createElement('button');
            button.dataset.imageId = item.key;
            button.className = 'tile-item' + (item.key === selectedKey ? ' active' : '');
            const canvas = document.createElement('canvas');
            canvas.width = item.entry.width*6; canvas.height = item.entry.height*6;
            drawEntry(canvas.getContext('2d'), item.entry, 0, 0, 6);
            const text = document.createElement('span');
            text.textContent = item.label;
            const detail = document.createElement('span');
            detail.className = 'binding'; detail.textContent = item.detail;
            text.appendChild(detail);
            button.append(canvas, text);
            button.addEventListener('click', () => { selectedKey = item.key; animationFrame = 0; activePen = Math.min(activePen, Model.penCount(item.entry)-1); renderAll(); });
            el.tileList.appendChild(button);
        });
    }

    function renderAll() {
        renderPalette();
        renderHeader();
        renderEditor();
        renderPreviews();
        renderLibrary();
        renderAnimation();
    }

    // ----------------------------------------------------------------- edits

    function pixelAt(event) {
        const rect = el.editorCanvas.getBoundingClientRect();
        const x = Math.floor((event.clientX - rect.left) / rect.width * selected().entry.width);
        const y = Math.floor((event.clientY - rect.top) / rect.height * selected().entry.height);
        return { x, y };
    }

    function paint(event) {
        const item = selected();
        if (!item) return;
        const { x, y } = pixelAt(event);
        if (x < 0 || x >= selected().entry.width || y < 0 || y >= selected().entry.height) return;
        // Only snapshot when a stroke actually changes something, so undo
        // steps map to edits rather than to mouse movements.
        if (Model.getPixel(item.entry, x, y) === activePen) return;
        if (!strokeStarted) { snapshot(); strokeStarted = true; }
        if (Model.setPixel(item.entry, x, y, activePen)) {
            setDirty(true);
            renderEditor();
            renderPreviews();
            renderLibrary();
            renderAnimation();
        }
    }

    function pickPen(event) {
        const item = selected();
        if (!item) return;
        const { x, y } = pixelAt(event);
        const pen = Model.getPixel(item.entry, x, y);
        if (pen >= 0) { activePen = pen; renderPalette(); }
    }

    function applyTransform(action) {
        const item = selected();
        if (!item) return;
        snapshot();
        Model.transform(item.entry, action);
        setDirty(true);
        renderAll();
    }

    // ------------------------------------------------------------- load/save

    function loadTileset(value, name) {
        try {
            Model.validateTileset(value);
        } catch (error) {
            window.alert(`That file is not a usable Lynx tileset.\n\n${error.message}`);
            return;
        }
        tileset = value;
        activePen = 1;
        animationFrame = 0;
        fileName = name || fileName;
        selectedKey = Model.entries(tileset)[0].key;
        undoStack.length = 0;
        redoStack.length = 0;
        updateUndoButtons();
        setDirty(false);
        renderAll();
    }

    function saveTileset() {
        if (!tileset) return;
        const text = JSON.stringify(tileset, null, 2) + '\n';
        const url = URL.createObjectURL(new Blob([text], { type: 'application/json' }));
        const link = document.createElement('a');
        link.href = url;
        link.download = fileName;
        link.click();
        URL.revokeObjectURL(url);
        setDirty(false);
    }

    function wire() {
        el.loadTileset.addEventListener('click', () => el.tilesetInput.click());
        el.tilesetInput.addEventListener('change', event => {
            const file = event.target.files[0];
            if (!file) return;
            const reader = new FileReader();
            reader.onload = () => {
                try {
                    loadTileset(JSON.parse(reader.result), file.name);
                } catch (error) {
                    window.alert(`Could not parse that file as JSON.\n\n${error.message}`);
                }
            };
            reader.readAsText(file);
            event.target.value = '';
        });
        el.saveTileset.addEventListener('click', saveTileset);
        el.undo.addEventListener('click', () => stepHistory(undoStack, redoStack));
        el.redo.addEventListener('click', () => stepHistory(redoStack, undoStack));

        el.editorCanvas.addEventListener('mousedown', event => {
            if (event.button === 2) { pickPen(event); return; }
            if (event.button !== 0) return;
            painting = true; strokeStarted = false; event.preventDefault();
            paint(event);
        });
        el.editorCanvas.addEventListener('mousemove', event => { if (painting) paint(event); });
        el.editorCanvas.addEventListener('contextmenu', event => event.preventDefault());
        window.addEventListener('mouseup', () => { painting = false; });

        document.querySelectorAll('[data-transform]').forEach(button => {
            button.addEventListener('click', () => applyTransform(button.dataset.transform));
        });
        el.libraryFilter.addEventListener('input', renderLibrary);
        el.libraryKind.addEventListener('change', renderLibrary);
        el.playerPreview.addEventListener('change', () => {
            previewPlayer = Number(el.playerPreview.value); renderAll();
        });
        el.animationPlay.addEventListener('click', () => {
            animationPlaying = !animationPlaying;
            el.animationPlay.textContent = animationPlaying ? 'Pause animation' : 'Play animation';
        });

        window.addEventListener('keydown', event => {
            if (event.target.tagName === 'INPUT' || event.target.tagName === 'SELECT') return;
            if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'z') {
                event.preventDefault();
                stepHistory(event.shiftKey ? redoStack : undoStack, event.shiftKey ? undoStack : redoStack);
                return;
            }
            const digit = parseInt(event.key, 16);
            if (selected() && !Number.isNaN(digit) && event.key.length === 1 && digit < Model.penCount(selected().entry)) { activePen = digit; renderPalette(); }
        });

        window.addEventListener('beforeunload', event => {
            if (dirty) { event.preventDefault(); event.returnValue = ''; }
        });
    }

    function renderAnimation() {
        const item = selected();
        const frames = tileset ? Model.animation(tileset, item?.entry) : [];
        el.animationPlay.disabled = !frames.length;
        const canvas = el.animationCanvas;
        if (!frames.length) {
            canvas.getContext('2d').clearRect(0,0,canvas.width,canvas.height);
            el.animationLabel.textContent = 'Select Dance or Explosion'; return;
        }
        const frame = frames[animationFrame % frames.length];
        canvas.width = frame.width*12; canvas.height = frame.height*12;
        drawEntry(canvas.getContext('2d'),frame,0,0,12);
        el.animationLabel.textContent = `${frame.name} · ${animationFrame % frames.length + 1}/${frames.length} · 133 ms`;
    }
    setInterval(() => {
        if (animationPlaying) { animationFrame++; renderAnimation(); }
    }, Model.FRAME_MS);

    wire();
    renderAll();
})();
