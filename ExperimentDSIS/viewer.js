import * as THREE from "three";
import { MTLLoader } from "three/addons/loaders/MTLLoader.js";
import { OBJLoader } from "three/addons/loaders/OBJLoader.js";

const state = {
  trials: [],
  index: 0,
  selectedScore: null,
  answers: [],
  trialStartedAt: performance.now(),
};

const STORAGE_KEY = "nextlife_dsis_distance_answers_v1";

const els = {
  progress: document.getElementById("progress"),
  progressCompact: document.getElementById("progressCompact"),
  objectName: document.getElementById("objectName"),
  distortionName: document.getElementById("distortionName"),
  scoreButtons: document.getElementById("scoreButtons"),
  comment: document.getElementById("comment"),
  status: document.getElementById("status"),
  nextBtn: document.getElementById("nextBtn"),
  prevBtn: document.getElementById("prevBtn"),
  exportCsvBtn: document.getElementById("exportCsvBtn"),
  exportJsonBtn: document.getElementById("exportJsonBtn"),
  scaleBtn: document.getElementById("scaleBtn"),
  scaleHelp: document.getElementById("scaleHelp"),
  resetBtn: document.getElementById("resetBtn"),
};

function baseDir(path) {
  return path.slice(0, path.lastIndexOf("/") + 1);
}

function assetUrl(path) {
  if (!path) return path;
  if (/^(https?:)?\/\//i.test(path) || path.startsWith("../")) return path;
  return `../${path.replaceAll("\\", "/")}`;
}

function fileName(path) {
  return path.split(/[\\/]/).pop();
}

class ModelViewer {
  constructor(canvas) {
    this.canvas = canvas;
    this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: false });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.setSize(960, 960, false);
    this.renderer.setClearColor(0xf1f4f2, 1);

    this.scene = new THREE.Scene();
    this.camera = new THREE.PerspectiveCamera(38, 1, 0.01, 1000);
    this.camera.position.set(0, 0.5, 4);

    this.group = new THREE.Group();
    this.scene.add(this.group);

    const hemi = new THREE.HemisphereLight(0xffffff, 0x48504d, 2.4);
    const key = new THREE.DirectionalLight(0xffffff, 2.1);
    key.position.set(4, 6, 5);
    const fill = new THREE.DirectionalLight(0xffffff, 0.9);
    fill.position.set(-5, 3, -4);
    this.scene.add(hemi, key, fill);
  }

  clear() {
    while (this.group.children.length) {
      const child = this.group.children.pop();
      child.traverse((node) => {
        if (node.geometry) node.geometry.dispose();
        if (node.material) {
          const materials = Array.isArray(node.material) ? node.material : [node.material];
          materials.forEach((mat) => {
            Object.values(mat).forEach((value) => {
              if (value && value.isTexture) value.dispose();
            });
            mat.dispose();
          });
        }
      });
    }
  }

  async load(spec) {
    this.clear();
    if (!spec || !spec.obj) throw new Error("Modele sans OBJ");

    const manager = new THREE.LoadingManager();
    if (spec.texture_folder) {
      const textureRoot = `${assetUrl(spec.texture_folder)}/`;
      manager.setURLModifier((url) => {
        if (/\.(png|jpg|jpeg|bmp|webp|tif|tiff)$/i.test(url)) {
          return textureRoot + fileName(url);
        }
        return url;
      });
    }

    const objLoader = new OBJLoader(manager);
    if (spec.mtl) {
      const mtlLoader = new MTLLoader(manager);
      mtlLoader.setPath(assetUrl(baseDir(spec.mtl)));
      mtlLoader.setResourcePath(assetUrl(baseDir(spec.mtl)));
      const materials = await mtlLoader.loadAsync(fileName(spec.mtl));
      materials.preload();
      this.normalizeMaterials(materials);
      objLoader.setMaterials(materials);
    }

    objLoader.setPath(assetUrl(baseDir(spec.obj)));
    const object = await objLoader.loadAsync(fileName(spec.obj));
    this.fit(object);
    this.group.add(object);
  }

  fit(object) {
    object.traverse((node) => {
      if (node.isMesh) {
        node.frustumCulled = false;
        const materials = Array.isArray(node.material) ? node.material : [node.material];
        materials.forEach((mat) => {
          if (mat) mat.side = THREE.DoubleSide;
        });
      }
    });

    const robustBox = this.robustBox(object);
    const box = robustBox || new THREE.Box3().setFromObject(object);
    const size = new THREE.Vector3();
    const center = new THREE.Vector3();
    box.getSize(size);
    box.getCenter(center);

    const maxDim = Math.max(size.x, size.y, size.z) || 1;
    const scale = 2.4 / maxDim;
    object.scale.setScalar(scale);
    object.position.set(-center.x * scale, -center.y * scale, -center.z * scale);

    this.camera.position.set(0, 0.1, 4.4);
    this.camera.lookAt(0, 0, 0);
  }

  normalizeMaterials(materials) {
    Object.values(materials.materials || {}).forEach((material) => {
      if (!material) return;
      if (material.emissive) {
        material.emissive.setRGB(0, 0, 0);
      }
      if (material.map && material.color) {
        material.color.setRGB(1, 1, 1);
      }
      material.needsUpdate = true;
    });
  }

  robustBox(object) {
    const xs = [];
    const ys = [];
    const zs = [];
    object.updateMatrixWorld(true);
    object.traverse((node) => {
      if (!node.isMesh || !node.geometry?.attributes?.position) return;
      const pos = node.geometry.attributes.position;
      const point = new THREE.Vector3();
      for (let i = 0; i < pos.count; i += Math.max(1, Math.floor(pos.count / 30000))) {
        point.fromBufferAttribute(pos, i).applyMatrix4(node.matrixWorld);
        if (Number.isFinite(point.x) && Number.isFinite(point.y) && Number.isFinite(point.z)) {
          xs.push(point.x);
          ys.push(point.y);
          zs.push(point.z);
        }
      }
    });
    if (xs.length < 8) return null;
    const pick = (values, q) => {
      values.sort((a, b) => a - b);
      return values[Math.max(0, Math.min(values.length - 1, Math.floor((values.length - 1) * q)))];
    };
    const min = new THREE.Vector3(pick(xs, 0.02), pick(ys, 0.02), pick(zs, 0.02));
    const max = new THREE.Vector3(pick(xs, 0.98), pick(ys, 0.98), pick(zs, 0.98));
    if (max.x <= min.x || max.y <= min.y || max.z <= min.z) return null;
    return new THREE.Box3(min, max);
  }

  render(deltaSeconds) {
    this.group.rotation.y += deltaSeconds * 0.45;
    this.renderer.render(this.scene, this.camera);
  }
}

const referenceViewer = new ModelViewer(document.getElementById("referenceCanvas"));
const distortedViewer = new ModelViewer(document.getElementById("distortedCanvas"));

function makeScoreButtons() {
  const labels = distanceScaleLabels();
  for (let score = 0; score <= 5; score += 1) {
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = String(score);
    button.title = labels[score];
    button.setAttribute("aria-label", `${score}: ${labels[score]}`);
    button.addEventListener("click", () => setScore(score));
    els.scoreButtons.appendChild(button);
  }
}

function distanceScaleLabels() {
  return {
    0: "Aucune distance: identique ou quasi identique a la reference",
    1: "Distance tres faible: differences a peine visibles",
    2: "Distance faible: differences visibles mais l'objet reste tres proche",
    3: "Distance moyenne: ressemblant, mais plusieurs changements nets",
    4: "Distance forte: meme categorie possible, objet tres modifie",
    5: "Distance maximale: plus de ressemblance exploitable",
  };
}

function setScore(score, persist = true) {
  state.selectedScore = score;
  [...els.scoreButtons.children].forEach((button) => {
    button.classList.toggle("selected", Number(button.textContent) === score);
  });
  if (persist && score >= 0) {
    recordAnswer();
    els.status.textContent = "Reponse sauvegardee.";
  }
}

function currentTrial() {
  return state.trials[state.index];
}

async function showTrial() {
  const trial = currentTrial();
  const saved = savedAnswerFor(trial.trial_id);
  state.selectedScore = saved ? Number(saved.distance_0_5) : null;
  els.comment.value = saved?.comment || "";
  setScore(saved ? state.selectedScore : -1, false);
  els.status.textContent = "Chargement des deux objets...";
  const progressText = `${state.index + 1} / ${state.trials.length}`;
  els.progress.textContent = progressText;
  els.progressCompact.textContent = progressText;
  els.objectName.textContent = trial.object_name || trial.object_id;
  els.distortionName.textContent = `${trial.distortion_family} / ${trial.distortion_profile}`;
  state.trialStartedAt = performance.now();

  try {
    await Promise.all([
      referenceViewer.load(trial.reference),
      distortedViewer.load(trial.distorted),
    ]);
    els.status.textContent = "";
  } catch (error) {
    els.status.textContent = `Chargement impossible: ${error.message}`;
  }
}

function savedAnswerFor(trialId) {
  return state.answers.find((item) => item.trial_id === trialId) || null;
}

function recordAnswer() {
  const trial = currentTrial();
  if (state.selectedScore === null || state.selectedScore < 0) return;
  const existingIndex = state.answers.findIndex((item) => item.trial_id === trial.trial_id);
  const answer = {
    timestamp: new Date().toISOString(),
    trial_id: trial.trial_id,
    object_id: trial.object_id,
    object_name: trial.object_name,
    imagenet_class: trial.imagenet_class,
    scene_id: trial.scene_id,
    face_count: trial.face_count,
    reference_policy: trial.reference_policy,
    reference_variant_id: trial.reference?.variant_id || "",
    distortion_modality: trial.distortion_modality,
    distortion_family: trial.distortion_family,
    distortion_profile: trial.distortion_profile,
    distorted_variant_id: trial.distorted?.variant_id || "",
    distance_0_5: state.selectedScore,
    comment: els.comment.value.trim(),
    elapsed_ms: Math.round(performance.now() - state.trialStartedAt),
    reference_obj: trial.reference?.obj || "",
    distorted_obj: trial.distorted?.obj || "",
    distortion_manifest: trial.distortion_manifest || "",
  };

  if (existingIndex >= 0) state.answers[existingIndex] = answer;
  else state.answers.push(answer);
  saveAnswers();
}

function saveAnswers() {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(state.answers));
}

function loadSavedAnswers() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    const parsed = raw ? JSON.parse(raw) : [];
    state.answers = Array.isArray(parsed) ? parsed : [];
  } catch {
    state.answers = [];
  }
}

function resetAnswers() {
  state.answers = [];
  localStorage.removeItem(STORAGE_KEY);
  state.selectedScore = null;
  els.comment.value = "";
  setScore(-1, false);
  els.status.textContent = "Reponses locales effacees.";
}

function nextTrial() {
  if (state.selectedScore === null || state.selectedScore < 0) {
    els.status.textContent = "Note manquante.";
    return;
  }
  recordAnswer();
  if (state.index < state.trials.length - 1) {
    state.index += 1;
    showTrial();
  } else {
    els.status.textContent = "Session terminee. Export disponible.";
  }
}

function previousTrial() {
  if (state.index > 0) {
    recordAnswer();
    state.index -= 1;
    showTrial();
  }
}

function csvEscape(value) {
  const text = String(value ?? "");
  return /[",\n\r]/.test(text) ? `"${text.replaceAll('"', '""')}"` : text;
}

function download(name, content, type) {
  const blob = new Blob([content], { type });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = name;
  link.click();
  URL.revokeObjectURL(url);
}

function exportJson() {
  download("dsis_distance_results.json", JSON.stringify(state.answers, null, 2), "application/json");
}

function exportCsv() {
  const headers = [
    "timestamp",
    "trial_id",
    "object_id",
    "object_name",
    "imagenet_class",
    "scene_id",
    "face_count",
    "reference_policy",
    "reference_variant_id",
    "distortion_modality",
    "distortion_family",
    "distortion_profile",
    "distorted_variant_id",
    "distance_0_5",
    "comment",
    "elapsed_ms",
    "reference_obj",
    "distorted_obj",
    "distortion_manifest",
  ];
  const rows = [headers.join(",")];
  state.answers.forEach((answer) => {
    rows.push(headers.map((header) => csvEscape(answer[header])).join(","));
  });
  download("dsis_distance_results.csv", `${rows.join("\n")}\n`, "text/csv");
}

async function loadExperiment() {
  const response = await fetch("./dsis_trials.json", { cache: "no-store" });
  if (!response.ok) throw new Error("dsis_trials.json introuvable");
  const payload = await response.json();
  state.trials = payload.trials || [];
  if (!state.trials.length) throw new Error("Aucun essai dans dsis_trials.json");
  loadSavedAnswers();
  await showTrial();
}

let last = performance.now();
function animate(now) {
  const delta = Math.min(0.05, (now - last) / 1000);
  last = now;
  referenceViewer.render(delta);
  distortedViewer.render(delta);
  requestAnimationFrame(animate);
}

makeScoreButtons();
els.nextBtn.addEventListener("click", nextTrial);
els.prevBtn.addEventListener("click", previousTrial);
els.exportJsonBtn.addEventListener("click", exportJson);
els.exportCsvBtn.addEventListener("click", exportCsv);
els.comment.addEventListener("input", () => {
  recordAnswer();
});
els.resetBtn.addEventListener("click", resetAnswers);
els.scaleBtn.addEventListener("click", () => {
  els.scaleHelp.hidden = !els.scaleHelp.hidden;
});
window.addEventListener("keydown", (event) => {
  if (/^[0-5]$/.test(event.key)) setScore(Number(event.key));
  if (event.key === "Enter" && !event.shiftKey) nextTrial();
});

loadExperiment().catch((error) => {
  els.status.textContent = error.message;
});
requestAnimationFrame(animate);
