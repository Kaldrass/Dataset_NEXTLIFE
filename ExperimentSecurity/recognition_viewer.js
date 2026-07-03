import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { MTLLoader } from "three/addons/loaders/MTLLoader.js";
import { OBJLoader } from "three/addons/loaders/OBJLoader.js";

const STORAGE_KEY = "nextlife_recognition_security_answers_v1";
const CURRENT_INDEX_KEY = "nextlife_recognition_security_current_index_v1";
const DEFAULT_SCENE_METRICS = {
  floorY: -1.25,
  center: new THREE.Vector3(0, 0, 0),
  inspectionPoint: new THREE.Vector3(0, -1.25, 0),
  size: new THREE.Vector3(8, 4.8, 8),
  objectHeightRatio: 0.18,
  objectFootprintRatio: 0.13,
  objectScaleMode: "height",
  objectMinHeight: 0.03,
  objectMaxHeight: 1.2,
  objectMinFootprint: 0.03,
  objectMaxFootprint: 1.2,
  objectLocation: null,
  objectLocationSpace: "world",
  objectPlacementMode: "floorCenter",
  sceneLocation: null,
  floorOffsetRatio: 0.04,
  cameraYaw: 0,
  cameraDistanceRatio: 0.34,
  cameraMinDistance: 0.25,
  cameraMaxDistance: 4.5,
  cameraHeightRatio: 0.18,
};

const SCENE_PLACEMENT = {
  art_gallery: {
    objectHeightRatio: 0.05,
    objectFootprintRatio: 0.03,
    objectScaleMode: "height",
    floorOffsetRatio: 0.0953,
    position: { x: 0, y: 0.05, z: 0 },
    cameraYaw: 0.5,
    cameraDistanceRatio: 0.01,
    cameraMinDistance: 0.08,
    cameraMaxDistance: 2.8,
    cameraHeightRatio: 0.2,
  },
  exterior: {
    objectHeightRatio: 0.14,
    objectFootprintRatio: 0.11,
    objectScaleMode: "height",
    sceneLocation: { x: 0, y: 1.31, z: 0},
    objectLocation: { x: -0.07, y: 0, z: 0 },
    objectLocationSpace: "world",
    objectPlacementMode: "origin",
    floorOffsetRatio: 0,
    position: { x: 0, z: 0 },
    cameraYaw: 0,
    cameraDistanceRatio: 0.26,
    cameraHeightRatio: 0.16,
  },
  exterior_city: {
    objectHeightRatio: 0.15,
    objectFootprintRatio: 0.1,
    objectScaleMode: "height",
    sceneLocation: { x: 0, y: 0.73, z: 0},
    objectLocation: { x: 0, y: 0.1, z: 0 },
    objectLocationSpace: "world",
    objectPlacementMode: "origin",
    floorOffsetRatio: 0.05,
    position: { x: 0, y: 0, z: 0 },
    cameraYaw: 0,
    cameraDistanceRatio: 0.25,
    cameraHeightRatio: 0.16,
  },
  room_white: {
    objectHeightRatio: 0.15,
    objectFootprintRatio: 0.13,
    objectScaleMode: "height",
    sceneLocation: { x: 0, y: 0.3, z: 0},
    objectLocation: { x: 0, y: 0.1, z: 0 },
    objectLocationSpace: "world",
    objectPlacementMode: "origin",
    floorOffsetRatio: 0.05,
    position: { x: 0, z: 0 },
    cameraYaw: 0,
    cameraDistanceRatio: 0.15,
    cameraHeightRatio: 0.18,
  },
  sitting_room: {
    objectHeightRatio: 0.15,
    objectFootprintRatio: 0.11,
    objectScaleMode: "height",
    floorOffsetRatio: 0.05,
    position: { x: 0, z: 0.05 },
    cameraYaw: 0,
    cameraDistanceRatio: 0.25,
    cameraHeightRatio: 0.17,
  },
};

const OBJECT_PLACEMENT = {
  labels: {
    "running shoe": {
      rotationDeg: { x: 0, y: 0, z: 0 },
    },
  },
  objects: {
  },
};

const state = {
  trials: [],
  index: 0,
  answers: [],
  selectedLabel: "",
  selectedSecurity: "",
  trialStartedAt: performance.now(),
};

const els = {
  progress: document.getElementById("progress"),
  objectCode: document.getElementById("objectCode"),
  distortionName: document.getElementById("distortionName"),
  labelChoices: document.getElementById("labelChoices"),
  securityChoices: document.getElementById("securityChoices"),
  comment: document.getElementById("comment"),
  status: document.getElementById("status"),
  nextBtn: document.getElementById("nextBtn"),
  prevBtn: document.getElementById("prevBtn"),
  resetBtn: document.getElementById("resetBtn"),
  exportCsvBtn: document.getElementById("exportCsvBtn"),
  exportJsonBtn: document.getElementById("exportJsonBtn"),
  jumpInput: document.getElementById("jumpInput"),
  jumpBtn: document.getElementById("jumpBtn"),
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

function vectorFrom(value, fallback = new THREE.Vector3()) {
  if (!value) return fallback.clone();
  if (Array.isArray(value)) {
    return new THREE.Vector3(value[0] || 0, value[1] || 0, value[2] || 0);
  }
  return new THREE.Vector3(value.x || 0, value.y || 0, value.z || 0);
}

function eulerDegFrom(value) {
  const rotation = vectorFrom(value);
  return new THREE.Euler(
    THREE.MathUtils.degToRad(rotation.x),
    THREE.MathUtils.degToRad(rotation.y),
    THREE.MathUtils.degToRad(rotation.z),
    "XYZ"
  );
}

function objectPlacementFor(trial) {
  if (!trial) return {};
  return {
    ...(OBJECT_PLACEMENT.labels[trial.imagenet_class] || {}),
    ...(OBJECT_PLACEMENT.objects[trial.object_id] || {}),
  };
}

class ModelViewer {
  constructor(canvas) {
    this.canvas = canvas;
    this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: false });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.setClearColor(0xe4ebe8, 1);

    this.scene = new THREE.Scene();
    this.camera = new THREE.PerspectiveCamera(38, 1, 0.01, 1000);
    this.camera.position.set(0, 0.2, 4.2);

    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.08;
    this.controls.enablePan = false;
    this.controls.minDistance = 1.2;
    this.controls.maxDistance = 8;

    this.group = new THREE.Group();
    this.sceneGroup = new THREE.Group();
    this.currentSceneId = "";
    this.sceneMetrics = { ...DEFAULT_SCENE_METRICS };
    this.gltfLoader = new GLTFLoader();
    this.activeSceneRoot = null;
    this.scene.add(this.sceneGroup);
    this.scene.add(this.group);

    const hemi = new THREE.HemisphereLight(0xffffff, 0x4d5754, 2.3);
    const key = new THREE.DirectionalLight(0xffffff, 2.1);
    key.position.set(4, 6, 5);
    const fill = new THREE.DirectionalLight(0xffffff, 0.9);
    fill.position.set(-5, 3, -4);
    this.scene.add(hemi, key, fill);
    this.makeProceduralScene("room_white");
    this.resize();
    window.addEventListener("resize", () => this.resize());
  }

  resize() {
    const size = Math.max(1, Math.floor(this.canvas.getBoundingClientRect().width));
    this.renderer.setSize(size, size, false);
    this.camera.aspect = 1;
    this.camera.updateProjectionMatrix();
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

  async load(spec, trial = null) {
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
    this.fit(object, objectPlacementFor(trial));
    this.group.add(object);
  }

  disposeObject(object) {
    object.traverse((node) => {
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

  clearSceneGroup() {
    while (this.sceneGroup.children.length) {
      const child = this.sceneGroup.children.pop();
      this.disposeObject(child);
    }
    this.activeSceneRoot = null;
  }

  async setScene(sceneId) {
    const nextSceneId = sceneId || "room_white";
    if (nextSceneId === this.currentSceneId) return true;
    this.currentSceneId = nextSceneId;
    this.clearSceneGroup();

    try {
      await this.loadGltfScene(nextSceneId);
      return true;
    } catch {
      this.makeProceduralScene(nextSceneId);
      return false;
    }
  }

  async loadGltfScene(sceneId) {
    const placement = SCENE_PLACEMENT[sceneId] || {};
    const scenePath = `Scenes/${sceneId}/scene.gltf`;
    const gltf = await this.gltfLoader.loadAsync(assetUrl(scenePath));
    const root = gltf.scene;
    root.traverse((node) => {
      if (!node.isMesh) return;
      node.frustumCulled = false;
      const materials = Array.isArray(node.material) ? node.material : [node.material];
      materials.forEach((mat) => {
        if (!mat) return;
        mat.side = THREE.DoubleSide;
        mat.depthWrite = true;
      });
    });

    const box = new THREE.Box3().setFromObject(root);
    const size = new THREE.Vector3();
    const center = new THREE.Vector3();
    box.getSize(size);
    box.getCenter(center);
    const maxDim = Math.max(size.x, size.y, size.z) || 1;
    const scale = 5.8 / maxDim;
    root.scale.setScalar(scale);
    root.position.set(-center.x * scale, -box.min.y * scale - 1.35, -center.z * scale);
    if (placement.sceneLocation) {
      root.position.add(vectorFrom(placement.sceneLocation));
    }
    root.rotation.set(0, 0, 0);
    root.updateMatrixWorld(true);

    this.scene.background = new THREE.Color(0xe6ece9);
    this.renderer.setClearColor(0xe6ece9, 1);
    this.sceneGroup.add(root);
    this.activeSceneRoot = root;
    this.updateSceneMetrics(sceneId);
  }

  scenePointToWorld(point) {
    const local = vectorFrom(point);
    if (this.activeSceneRoot) {
      return this.activeSceneRoot.localToWorld(local);
    }
    return local;
  }

  updateSceneMetrics(sceneId) {
    const box = new THREE.Box3().setFromObject(this.sceneGroup);
    const size = new THREE.Vector3();
    const center = new THREE.Vector3();
    box.getSize(size);
    box.getCenter(center);
    const placement = SCENE_PLACEMENT[sceneId] || {};
    const floorOffsetRatio = placement.floorOffsetRatio ?? DEFAULT_SCENE_METRICS.floorOffsetRatio;
    const floorY = Number.isFinite(box.min.y)
      ? box.min.y + Math.max(size.y, 1) * floorOffsetRatio
      : DEFAULT_SCENE_METRICS.floorY;
    const position = placement.position || {};
    const inspectionPoint = new THREE.Vector3(
      center.x + (position.x || 0) * size.x,
      floorY + (position.y || 0) * size.y,
      center.z + (position.z || 0) * size.z
    );
    this.sceneMetrics = {
      bounds: box,
      floorY,
      center,
      inspectionPoint,
      size,
      objectHeightRatio: placement.objectHeightRatio || DEFAULT_SCENE_METRICS.objectHeightRatio,
      objectFootprintRatio: placement.objectFootprintRatio || DEFAULT_SCENE_METRICS.objectFootprintRatio,
      objectScaleMode: placement.objectScaleMode || DEFAULT_SCENE_METRICS.objectScaleMode,
      objectMinHeight: placement.objectMinHeight || DEFAULT_SCENE_METRICS.objectMinHeight,
      objectMaxHeight: placement.objectMaxHeight || DEFAULT_SCENE_METRICS.objectMaxHeight,
      objectMinFootprint: placement.objectMinFootprint || DEFAULT_SCENE_METRICS.objectMinFootprint,
      objectMaxFootprint: placement.objectMaxFootprint || DEFAULT_SCENE_METRICS.objectMaxFootprint,
      objectLocation: placement.objectLocation || DEFAULT_SCENE_METRICS.objectLocation,
      objectLocationSpace: placement.objectLocationSpace || DEFAULT_SCENE_METRICS.objectLocationSpace,
      objectPlacementMode: placement.objectPlacementMode || DEFAULT_SCENE_METRICS.objectPlacementMode,
      cameraYaw: placement.cameraYaw ?? DEFAULT_SCENE_METRICS.cameraYaw,
      cameraDistanceRatio: placement.cameraDistanceRatio ?? DEFAULT_SCENE_METRICS.cameraDistanceRatio,
      cameraMinDistance: placement.cameraMinDistance ?? DEFAULT_SCENE_METRICS.cameraMinDistance,
      cameraMaxDistance: placement.cameraMaxDistance ?? DEFAULT_SCENE_METRICS.cameraMaxDistance,
      cameraHeightRatio: placement.cameraHeightRatio ?? DEFAULT_SCENE_METRICS.cameraHeightRatio,
    };
  }

  makeProceduralScene(sceneId) {
    this.clearSceneGroup();
    const presets = {
      art_gallery: { bg: 0xf0eee8, floor: 0xd8d2c6, wall: 0xf7f4ec, accent: 0xb8aa92 },
      exterior: { bg: 0xdceaf2, floor: 0x8f9f78, wall: 0xc8d8df, accent: 0x6c7c5c },
      exterior_city: { bg: 0xd9e2e7, floor: 0x9aa0a0, wall: 0xc8ced2, accent: 0x6f787d },
      room_white: { bg: 0xe9eeeb, floor: 0xd9dfdc, wall: 0xf6f7f5, accent: 0xc9d0cc },
      sitting_room: { bg: 0xeadfd4, floor: 0xb49a7e, wall: 0xead8c7, accent: 0x8a6d56 },
    };
    const preset = presets[sceneId] || presets.room_white;
    this.renderer.setClearColor(preset.bg, 1);
    this.scene.background = new THREE.Color(preset.bg);

    const floor = new THREE.Mesh(
      new THREE.PlaneGeometry(8, 8),
      new THREE.MeshStandardMaterial({ color: preset.floor, roughness: 0.92, metalness: 0 })
    );
    floor.rotation.x = -Math.PI / 2;
    floor.position.y = -1.25;
    floor.receiveShadow = true;
    this.sceneGroup.add(floor);

    const wall = new THREE.Mesh(
      new THREE.PlaneGeometry(8, 4.8),
      new THREE.MeshStandardMaterial({ color: preset.wall, roughness: 0.96, metalness: 0 })
    );
    wall.position.set(0, 1.15, -2.55);
    this.sceneGroup.add(wall);

    if (sceneId === "exterior_city") {
      [-2.8, 2.8].forEach((x) => {
        const block = new THREE.Mesh(
          new THREE.BoxGeometry(0.75, 1.5, 0.6),
          new THREE.MeshStandardMaterial({ color: preset.accent, roughness: 0.88 })
        );
        block.position.set(x, -0.5, -2.15);
        this.sceneGroup.add(block);
      });
    } else if (sceneId === "art_gallery") {
      [-2.4, 2.4].forEach((x) => {
        const frame = new THREE.Mesh(
          new THREE.BoxGeometry(0.9, 0.7, 0.04),
          new THREE.MeshStandardMaterial({ color: preset.accent, roughness: 0.7 })
        );
        frame.position.set(x, 1.2, -2.48);
        this.sceneGroup.add(frame);
      });
    } else if (sceneId === "sitting_room") {
      const plinth = new THREE.Mesh(
        new THREE.BoxGeometry(3.8, 0.22, 0.9),
        new THREE.MeshStandardMaterial({ color: preset.accent, roughness: 0.86 })
      );
      plinth.position.set(0, -1.12, -1.65);
      this.sceneGroup.add(plinth);
    }
    this.updateSceneMetrics(sceneId);
  }

  fit(object, objectPlacement = {}) {
    object.traverse((node) => {
      if (node.isMesh) {
        node.frustumCulled = false;
        const materials = Array.isArray(node.material) ? node.material : [node.material];
        materials.forEach((mat) => {
          if (mat) mat.side = THREE.DoubleSide;
        });
      }
    });

    const box = this.robustBox(object) || new THREE.Box3().setFromObject(object);
    const size = new THREE.Vector3();
    const center = new THREE.Vector3();
    box.getSize(size);
    box.getCenter(center);

    const sceneSize = this.sceneMetrics.size || DEFAULT_SCENE_METRICS.size;
    const sceneHeight = Math.max(sceneSize.y || 0, 1);
    const sceneFootprint = Math.max(Math.min(sceneSize.x || 0, sceneSize.z || 0), 1);
    const targetHeight = THREE.MathUtils.clamp(
      sceneHeight * this.sceneMetrics.objectHeightRatio,
      this.sceneMetrics.objectMinHeight,
      this.sceneMetrics.objectMaxHeight
    );
    const targetFootprint = THREE.MathUtils.clamp(
      sceneFootprint * this.sceneMetrics.objectFootprintRatio,
      this.sceneMetrics.objectMinFootprint,
      this.sceneMetrics.objectMaxFootprint
    );
    const objectHeight = Math.max(size.y, 0.0001);
    const objectFootprint = Math.max(size.x, size.z, 0.0001);
    const heightScale = targetHeight / objectHeight;
    const footprintScale = targetFootprint / objectFootprint;
    const scale = this.sceneMetrics.objectScaleMode === "fit"
      ? Math.min(heightScale, footprintScale)
      : heightScale;
    const floorY = this.sceneMetrics.floorY ?? DEFAULT_SCENE_METRICS.floorY;
    let inspectionPoint = this.sceneMetrics.inspectionPoint || DEFAULT_SCENE_METRICS.inspectionPoint;
    if (this.sceneMetrics.objectLocation) {
      inspectionPoint = this.sceneMetrics.objectLocationSpace === "scene"
        ? this.scenePointToWorld(this.sceneMetrics.objectLocation)
        : vectorFrom(this.sceneMetrics.objectLocation);
    }
    object.scale.setScalar(scale);
    if (this.sceneMetrics.objectPlacementMode === "origin") {
      object.position.copy(inspectionPoint);
    } else {
      object.position.set(
        inspectionPoint.x - center.x * scale,
        floorY - box.min.y * scale + 0.015,
        inspectionPoint.z - center.z * scale
      );
    }
    object.rotation.copy(eulerDegFrom(objectPlacement.rotationDeg));
    object.updateMatrixWorld(true);

    const fittedBox = new THREE.Box3().setFromObject(object);
    const fittedCenter = new THREE.Vector3();
    const fittedSize = new THREE.Vector3();
    fittedBox.getCenter(fittedCenter);
    fittedBox.getSize(fittedSize);
    const objectViewHeight = Math.max(fittedSize.y, targetHeight, 0.0001);

    const cameraDistance = THREE.MathUtils.clamp(
      sceneFootprint * this.sceneMetrics.cameraDistanceRatio,
      this.sceneMetrics.cameraMinDistance,
      this.sceneMetrics.cameraMaxDistance
    );
    const yaw = this.sceneMetrics.cameraYaw || 0;
    const cameraOffset = new THREE.Vector3(Math.sin(yaw) * cameraDistance, 0, Math.cos(yaw) * cameraDistance);
    const target = new THREE.Vector3(
      fittedCenter.x,
      fittedBox.min.y + objectViewHeight * 0.52,
      fittedCenter.z
    );
    this.camera.position.set(
      target.x + cameraOffset.x,
      target.y + Math.max(sceneHeight * this.sceneMetrics.cameraHeightRatio, objectViewHeight * 0.35),
      target.z + cameraOffset.z
    );
    this.controls.target.copy(target);
    this.camera.lookAt(target);
    this.controls.minDistance = Math.max(0.25, objectViewHeight * 0.35);
    this.controls.maxDistance = Math.max(3.5, cameraDistance * 2.2);
    this.controls.update();
  }

  normalizeMaterials(materials) {
    Object.values(materials.materials || {}).forEach((material) => {
      if (!material) return;
      if (material.emissive) material.emissive.setRGB(0, 0, 0);
      if (material.map && material.color) material.color.setRGB(1, 1, 1);
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
      const step = Math.max(1, Math.floor(pos.count / 30000));
      for (let i = 0; i < pos.count; i += step) {
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

  render() {
    this.controls.update();
    this.renderer.render(this.scene, this.camera);
  }
}

const viewer = new ModelViewer(document.getElementById("distortedCanvas"));

function currentTrial() {
  return state.trials[state.index];
}

function savedAnswerFor(trialId) {
  return state.answers.find((item) => item.trial_id === trialId) || null;
}

function saveCurrentIndex() {
  localStorage.setItem(CURRENT_INDEX_KEY, String(state.index));
}

function restoreCurrentIndex() {
  const saved = Number(localStorage.getItem(CURRENT_INDEX_KEY));
  if (Number.isInteger(saved) && saved >= 0 && saved < state.trials.length) {
    state.index = saved;
  }
}

function renderChoiceButtons(container, values, selectedValue, onSelect) {
  container.replaceChildren();
  values.forEach((value) => {
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = value;
    button.classList.toggle("selected", value === selectedValue);
    button.addEventListener("click", () => onSelect(value));
    container.appendChild(button);
  });
}

function setLabel(value, persist = true) {
  state.selectedLabel = value;
  renderChoiceButtons(els.labelChoices, currentTrial().label_choices || [], value, setLabel);
  if (persist) recordAnswer();
}

function setSecurity(value, persist = true) {
  state.selectedSecurity = value;
  renderChoiceButtons(els.securityChoices, currentTrial().security_levels || [], value, setSecurity);
  if (persist) recordAnswer();
}

async function showTrial() {
  const trial = currentTrial();
  const saved = savedAnswerFor(trial.trial_id);
  state.selectedLabel = saved?.chosen_label || "";
  state.selectedSecurity = saved?.security_level || "";
  els.comment.value = saved?.comment || "";
  els.progress.textContent = `${state.index + 1} / ${state.trials.length}`;
  els.jumpInput.value = String(state.index + 1);
  els.jumpInput.max = String(state.trials.length);
  els.objectCode.textContent = trial.object_id;
  els.distortionName.textContent = `${trial.distortion_family} / ${trial.distortion_profile}`;
  renderChoiceButtons(els.labelChoices, trial.label_choices || [], state.selectedLabel, setLabel);
  renderChoiceButtons(els.securityChoices, trial.security_levels || [], state.selectedSecurity, setSecurity);
  state.trialStartedAt = performance.now();
  saveCurrentIndex();
  els.status.textContent = "Chargement...";
  try {
    const realSceneLoaded = await viewer.setScene(trial.scene_id);
    await viewer.load(trial.distorted, trial);
    els.status.textContent = realSceneLoaded ? "" : "Scene procedurale utilisee.";
  } catch (error) {
    els.status.textContent = `Chargement impossible: ${error.message}`;
  }
}

function recordAnswer() {
  const trial = currentTrial();
  if (!trial || (!state.selectedLabel && !state.selectedSecurity && !els.comment.value.trim())) return;
  const existingIndex = state.answers.findIndex((item) => item.trial_id === trial.trial_id);
  const answer = {
    timestamp: new Date().toISOString(),
    trial_id: trial.trial_id,
    object_id: trial.object_id,
    object_name: trial.object_name,
    imagenet_class: trial.imagenet_class,
    chosen_label: state.selectedLabel,
    label_correct: state.selectedLabel ? state.selectedLabel === trial.imagenet_class : "",
    security_level: state.selectedSecurity,
    scene_id: trial.scene_id,
    face_count: trial.face_count,
    distortion_modality: trial.distortion_modality,
    distortion_family: trial.distortion_family,
    distortion_profile: trial.distortion_profile,
    distorted_variant_id: trial.distorted?.variant_id || "",
    elapsed_ms: Math.round(performance.now() - state.trialStartedAt),
    comment: els.comment.value.trim(),
    distorted_obj: trial.distorted?.obj || "",
    distortion_manifest: trial.distortion_manifest || "",
  };
  if (existingIndex >= 0) state.answers[existingIndex] = answer;
  else state.answers.push(answer);
  localStorage.setItem(STORAGE_KEY, JSON.stringify(state.answers));
}

function loadSavedAnswers() {
  try {
    const parsed = JSON.parse(localStorage.getItem(STORAGE_KEY) || "[]");
    state.answers = Array.isArray(parsed) ? parsed : [];
  } catch {
    state.answers = [];
  }
}

function nextTrial() {
  if (!state.selectedLabel || !state.selectedSecurity) {
    els.status.textContent = "Label et securite requis.";
    return;
  }
  recordAnswer();
  if (state.index < state.trials.length - 1) {
    state.index += 1;
    showTrial();
  } else {
    els.status.textContent = "Session terminee.";
  }
}

function previousTrial() {
  recordAnswer();
  if (state.index > 0) {
    state.index -= 1;
    showTrial();
  }
}

function jumpToTrial() {
  const requested = Number(els.jumpInput.value);
  if (!Number.isInteger(requested) || requested < 1 || requested > state.trials.length) {
    els.status.textContent = `Numero attendu entre 1 et ${state.trials.length}.`;
    els.jumpInput.value = String(state.index + 1);
    return;
  }
  recordAnswer();
  state.index = requested - 1;
  showTrial();
}

function resetAnswers() {
  state.answers = [];
  localStorage.removeItem(STORAGE_KEY);
  localStorage.removeItem(CURRENT_INDEX_KEY);
  state.index = 0;
  state.selectedLabel = "";
  state.selectedSecurity = "";
  els.comment.value = "";
  showTrial();
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
  download("nextlife_recognition_security_results.json", JSON.stringify(state.answers, null, 2), "application/json");
}

function exportCsv() {
  const headers = [
    "timestamp",
    "trial_id",
    "object_id",
    "object_name",
    "imagenet_class",
    "chosen_label",
    "label_correct",
    "security_level",
    "scene_id",
    "face_count",
    "distortion_modality",
    "distortion_family",
    "distortion_profile",
    "distorted_variant_id",
    "elapsed_ms",
    "comment",
    "distorted_obj",
    "distortion_manifest",
  ];
  const rows = [headers.join(",")];
  state.answers.forEach((answer) => {
    rows.push(headers.map((header) => csvEscape(answer[header])).join(","));
  });
  download("nextlife_recognition_security_results.csv", `${rows.join("\n")}\n`, "text/csv");
}

async function loadExperiment() {
  const response = await fetch("./recognition_trials.json", { cache: "no-store" });
  if (!response.ok) throw new Error("recognition_trials.json introuvable");
  const payload = await response.json();
  state.trials = payload.trials || [];
  if (!state.trials.length) throw new Error("Aucun essai dans recognition_trials.json");
  loadSavedAnswers();
  restoreCurrentIndex();
  await showTrial();
}

function animate() {
  viewer.render();
  requestAnimationFrame(animate);
}

els.nextBtn.addEventListener("click", nextTrial);
els.prevBtn.addEventListener("click", previousTrial);
els.resetBtn.addEventListener("click", resetAnswers);
els.exportCsvBtn.addEventListener("click", exportCsv);
els.exportJsonBtn.addEventListener("click", exportJson);
els.jumpBtn.addEventListener("click", jumpToTrial);
els.jumpInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    event.preventDefault();
    jumpToTrial();
  }
});
els.comment.addEventListener("input", recordAnswer);
window.addEventListener("keydown", (event) => {
  if (event.target instanceof HTMLInputElement || event.target instanceof HTMLTextAreaElement) return;
  if (event.key === "Enter" && !event.shiftKey) nextTrial();
});

loadExperiment().catch((error) => {
  els.status.textContent = error.message;
});
requestAnimationFrame(animate);
