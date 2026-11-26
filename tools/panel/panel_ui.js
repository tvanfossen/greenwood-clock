/**
 * @fileoverview The phone panel's native overlays.
 * @brief Typing with the phone's own keyboard, and cropping a picture inside the clock's frame
 *        with the real clock drawn over it (the panel renders it, panel_native.c). Colours come
 *        from the theme (CSS variables the panel sets: --screen, --raised, --inset, --text, ...).
 * @version 1
 */
(function () {
  'use strict';
  const W = 1024, H = 600;
  const KINDS = ['background', 'photo'];

  /**
   * An element with @p attrs, appended to @p parent when given.
   * @version 1
   * @utility
   * @return {HTMLElement} The element.
   */
  function el(tag, attrs, parent) {
    const e = document.createElement(tag);
    Object.assign(e, attrs || {});
    if (parent) parent.appendChild(e);
    return e;
  }

  /**
   * A full-screen overlay over the panel.
   * @version 1
   * @utility
   * @return {HTMLElement} It.
   */
  function overlay() {
    return el('div', { className: 'ov' }, document.body);
  }

  /**
   * Tell the panel the text overlay closed: @p ok with @p value, or cancelled.
   * @version 1
   * @utility
   */
  function textDone(ok, value) {
    Module.ccall('panel_text_done', null, ['number', 'string'], [ok ? 1 : 0, ok ? value : '']);
  }

  /**
   * Close the text overlay @p ov: @p ok with @p input's text, or cancelled.
   * @version 1
   * @utility
   */
  function closeText(ov, input, ok) {
    const v = input.value;
    ov.remove();
    textDone(ok, v);
  }

  /**
   * Type a line with the phone's own keyboard (called by the panel: panel_native_edit_text).
   * @version 1
   * @req REQ-UI-005
   */
  window.panelText = function (title, initial, max) {
    const ov = overlay();
    const box = el('div', { className: 'ov-box' }, ov);
    el('div', { className: 'ov-title', textContent: title }, box);
    const input = el('input', { id: 'text-input', className: 'ov-input', value: initial,
                                maxLength: max, autocomplete: 'off' }, box);
    const row = el('div', { className: 'ov-row' }, box);
    el('button', { id: 'text-cancel', className: 'ov-btn', textContent: 'Cancel',
                   onclick: () => closeText(ov, input, false) }, row);
    el('button', { id: 'text-done', className: 'ov-btn ov-go', textContent: 'Done',
                   onclick: () => closeText(ov, input, true) }, row);
    input.addEventListener('keydown', e => { if (e.key === 'Enter') closeText(ov, input, true); });
    input.focus();   // the phone's keyboard (an iPhone may need a tap on the field)
  };

  /**
   * A safe file name from the picked file's.
   * @version 1
   * @utility
   * @return {string} "name.png".
   */
  function safeName(file) {
    const base = file.name.replace(/\.[^.]*$/, '').replace(/[^A-Za-z0-9_-]+/g, '_').slice(0, 40);
    return (base || 'picture') + '.png';
  }

  /**
   * The theme colour CSS variable @p name holds.
   * @version 1
   * @utility
   * @return {string} A CSS colour.
   */
  function cssColor(name) {
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || 'black';
  }

  /**
   * The crop: the picture, its scale and offset in the clock's 1024x600 pixels; an album photo
   * starts whole (fit), a background fills the screen.
   * @version 1
   * @utility
   */
  function Crop(img, kind) {
    this.img = img;
    this.photo = kind === 1;
    this.fit = this.photo;
    this.reset();
  }
  Crop.prototype.cover = function () { return Math.max(W / this.img.width, H / this.img.height); };
  Crop.prototype.contain = function () { return Math.min(W / this.img.width, H / this.img.height); };
  Crop.prototype.reset = function () {
    this.s = this.fit ? this.contain() : this.cover();
    this.x = (W - this.img.width * this.s) / 2;
    this.y = (H - this.img.height * this.s) / 2;
  };
  Crop.prototype.clamp = function () {   // a background covers the screen: no gaps
    const min = this.fit ? this.contain() * 0.5 : this.cover();
    this.s = Math.min(Math.max(this.s, min), this.cover() * 8);
    const w = this.img.width * this.s, h = this.img.height * this.s;
    if (!this.fit) {
      this.x = Math.min(0, Math.max(W - w, this.x));
      this.y = Math.min(0, Math.max(H - h, this.y));
    }
  };
  Crop.prototype.zoom = function (k, cx, cy) {   // about (cx, cy)
    const s = this.s;
    this.s *= k;
    this.clamp();
    const r = this.s / s;
    this.x = cx - (cx - this.x) * r;
    this.y = cy - (cy - this.y) * r;
    this.clamp();
  };
  Crop.prototype.render = function (ctx) {   // the picture as the clock would show it
    ctx.fillStyle = cssColor('--screen');
    ctx.fillRect(0, 0, W, H);
    if (this.photo && this.fit) {   // a whole photo over a blurred copy of itself
      const s = this.cover();
      ctx.filter = 'blur(24px)';
      ctx.drawImage(this.img, (W - this.img.width * s) / 2, (H - this.img.height * s) / 2,
                    this.img.width * s, this.img.height * s);
      ctx.filter = 'none';
    }
    ctx.drawImage(this.img, this.x, this.y, this.img.width * this.s, this.img.height * this.s);
  };

  /**
   * The real clock over the picture in @p out, in the colour the clock would pick over it:
   * full size for a background, minimised for an album @p photo (the panel renders it).
   * @version 1
   * @utility
   * @return {HTMLCanvasElement|null} The clock alone.
   */
  function clockGuide(out, photo) {
    const data = out.getContext('2d').getImageData(0, 0, W, H).data;
    Module.HEAPU8.set(data, Module._panel_crop_picture());
    const p = Module._panel_crop_overlay(photo ? 1 : 0);
    if (!p) return null;
    const g = el('canvas', { width: W, height: H });
    g.getContext('2d').putImageData(
      new ImageData(new Uint8ClampedArray(Module.HEAPU8.subarray(p, p + W * H * 4)), W, H), 0, 0);
    return g;
  }

  /**
   * Drag to pan and pinch (or scroll) to zoom @p crop on @p view; @p changed after each move.
   * @version 1
   * @utility
   */
  function gestures(view, crop, changed) {
    const pts = new Map();
    let last = null;
    const toPic = (e) => {
      const r = view.getBoundingClientRect();
      return { x: (e.clientX - r.left) * W / r.width, y: (e.clientY - r.top) * H / r.height };
    };
    const spread = () => {
      const [a, b] = [...pts.values()];
      return { d: Math.hypot(a.x - b.x, a.y - b.y), x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
    };
    const move = (e) => {
      pts.set(e.pointerId, toPic(e));
      if (pts.size === 2) {
        const s = spread();
        crop.zoom(s.d / (last.d || s.d), s.x, s.y);
        last = s;
      } else {
        const p = toPic(e);
        crop.x += p.x - last.x;
        crop.y += p.y - last.y;
        crop.clamp();
        last = p;
      }
      changed();
    };
    const up = (e) => { pts.delete(e.pointerId); last = pts.size === 1 ? [...pts.values()][0] : null; };
    view.addEventListener('pointerdown', e => {
      view.setPointerCapture(e.pointerId);
      pts.set(e.pointerId, toPic(e));
      last = pts.size === 2 ? spread() : toPic(e);
    });
    view.addEventListener('pointermove', e => { if (pts.has(e.pointerId)) move(e); });
    view.addEventListener('pointerup', up);
    view.addEventListener('pointercancel', up);
    view.addEventListener('wheel', e => {
      e.preventDefault();
      const p = toPic(e);
      crop.zoom(e.deltaY < 0 ? 1.1 : 1 / 1.1, p.x, p.y);
      changed();
    }, { passive: false });
  }

  /**
   * The crop's buttons in @p row: Cancel, Fill / Fit whole (an album photo), Send.
   * @version 1
   * @utility
   */
  function cropButtons(row, crop, out, finish, changed) {
    el('button', { id: 'crop-cancel', className: 'ov-btn', textContent: 'Cancel',
                   onclick: () => finish(null) }, row);
    if (crop.photo) {
      const fit = el('button', { id: 'crop-fit', className: 'ov-btn', textContent: 'Fill' }, row);
      fit.onclick = () => {
        crop.fit = !crop.fit;
        fit.textContent = crop.fit ? 'Fill' : 'Fit whole';
        crop.reset();
        changed();
      };
    }
    el('button', { id: 'crop-send', className: 'ov-btn ov-go', textContent: 'Send',
                   onclick: () => { crop.render(out.getContext('2d')); out.toBlob(finish, 'image/png'); } }, row);
  }

  /**
   * Crop @p img in the clock's frame with the real clock over it; @p done gets the PNG, or
   * null when cancelled.
   * @version 1
   * @req REQ-UI-005
   */
  function cropScreen(img, kind, done) {
    const ov = overlay();
    const box = el('div', { className: 'ov-crop' }, ov);
    el('div', { className: 'ov-title', textContent: kind === 1 ? 'Album photo' : 'Background' }, box);
    const view = el('canvas', { id: 'crop-view', className: 'crop-view', width: W, height: H }, box);
    el('div', { className: 'ov-hint',
                textContent: 'Drag to move, pinch or scroll to zoom. The clock shows as it will.' }, box);
    const crop = new Crop(img, kind);
    const out = el('canvas', { width: W, height: H });
    let guide = null, timer = 0;
    const paint = () => {
      crop.render(out.getContext('2d'));
      const v = view.getContext('2d');
      v.drawImage(out, 0, 0);
      if (guide) v.drawImage(guide, 0, 0);
    };
    const changed = () => {   // the guide follows once the picture settles
      paint();
      clearTimeout(timer);
      timer = setTimeout(() => { guide = clockGuide(out, crop.photo); paint(); }, 250);
    };
    const finish = (blob) => { ov.remove(); done(blob); };
    cropButtons(el('div', { className: 'ov-row' }, box), crop, out, finish, changed);
    gestures(view, crop, changed);
    changed();
  }

  /**
   * Tell the panel how the upload went.
   * @version 1
   * @utility
   */
  function uploadDone(ok, name, msg) {
    Module.ccall('panel_upload_done', null, ['number', 'string', 'string'], [ok ? 1 : 0, name, msg]);
  }

  /**
   * Send @p blob to the clock as @p name, a background or album photo (@p kind).
   * @version 1
   * @utility
   */
  async function send(blob, name, kind, token) {
    const r = await fetch('/api/upload?kind=' + KINDS[kind] + '&name=' + encodeURIComponent(name),
                          { method: 'POST', headers: { Authorization: 'Bearer ' + token }, body: blob });
    const msg = r.ok ? 'Sent: ' + name : (r.status === 401 ? 'Session ended: scan the code on the clock again'
                                                         : 'The clock refused it (' + r.status + ')');
    uploadDone(r.ok, name, msg);
  }

  /**
   * Pick a picture, crop it in the clock's frame, send it (called by the panel's Upload).
   * @version 1
   * @req REQ-UI-005
   */
  window.panelUpload = function (kind, token) {
    const input = el('input', { type: 'file', accept: 'image/*' });
    input.onchange = async () => {
      if (!input.files.length) return;
      const file = input.files[0];
      try {
        const img = await createImageBitmap(file);
        cropScreen(img, kind, blob => {
          if (blob) send(blob, safeName(file), kind, token).catch(e => uploadDone(false, '', String(e)));
          else uploadDone(false, '', 'Cancelled');
        });
      } catch (e) {
        uploadDone(false, '', String(e));
      }
    };
    input.click();
  };
})();
