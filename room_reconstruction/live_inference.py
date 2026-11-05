"""Own adapter for NVIDIA's published C-Fast-FoundationStereo ONNX model."""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import numpy as np

MODEL_SHA256 = "2c79bbb274dc0aef687a8732b987077e9e3aa26ff9dd653141cda48c57dad5d3"


def input_tensor(image):
    # TAO depth_net dataloader NormalizeImage: RGB / 255, ImageNet mean/std.
    rgb = np.repeat(image[None], 3, axis=0).astype(np.float32)/255
    return (rgb-np.asarray((.485, .456, .406), np.float32)[:, None, None])/np.asarray((.229, .224, .225), np.float32)[:, None, None]


class StereoInference:
    def __init__(self, runtime, width=512, height=384, *, cpu_development=False, batch=1, vram_mb=3072):
        self.width, self.height = width, height
        if batch not in (1,2) or not 1536<=vram_mb<=8192:raise ValueError('Invalid stereo memory profile')
        self.batch=batch
        if (width, height) not in ((320, 256), (512, 384), (640, 480)):
            raise ValueError("Unsupported bounded stereo profile")
        runtime = Path(runtime)
        self.dll_directories = []
        if os.name == "nt":
            for path in sorted({dll.parent for dll in (runtime/"vendor").rglob("*.dll")}):
                self.dll_directories.append(os.add_dll_directory(str(path)))
        import onnxruntime as ort
        if not cpu_development and "CUDAExecutionProvider" not in ort.get_available_providers():
            raise RuntimeError("ONNX Runtime CUDA provider is unavailable")
        model = runtime/"c-fast-foundationstereo.onnx"
        if model.stat().st_size != 103228473 or hashlib.sha256(model.read_bytes()).hexdigest() != MODEL_SHA256:
            raise ValueError("Stereo model integrity check failed")
        options = ort.SessionOptions()
        options.log_severity_level = 3
        options.intra_op_num_threads = 4
        options.enable_mem_pattern = False
        provider = "CPUExecutionProvider" if cpu_development else ("CUDAExecutionProvider", {
            "device_id": "0", "gpu_mem_limit": str(vram_mb*1024**2), "arena_extend_strategy": "kSameAsRequested",
            "cudnn_conv_algo_search": "HEURISTIC"})
        self.session = ort.InferenceSession(str(model), sess_options=options, providers=[provider])
        if not cpu_development and self.session.get_providers()[0] != "CUDAExecutionProvider":
            raise RuntimeError("GPU initialization failed; CPU fallback is disabled")
        self.provider = self.session.get_providers()[0]

    def disparity(self, images):
        if any(image.shape != (self.height, self.width) for image in images):
            raise ValueError("Neural stereo input extent changed")
        # The dynamic-batch model can run both matching directions together.
        # Smaller NVIDIA cards retain sequential bilateral inference.
        if self.batch==2:
            left=np.stack((input_tensor(images[0]),input_tensor(images[1][:,::-1])))
            right=np.stack((input_tensor(images[1]),input_tensor(images[0][:,::-1])))
            result=self.session.run(['disparity'],{'left_image':left,'right_image':right})[0]
            if result.shape!=(2,1,self.height,self.width) or not np.isfinite(result).all():
                raise ValueError('Unexpected batched neural stereo output')
            return [result[0,0].copy(),result[1,0,:,::-1].copy()]
        outputs = []
        for left, right, flip in ((images[0], images[1], False), (images[1][:, ::-1], images[0][:, ::-1], True)):
            result = self.session.run(["disparity"], {"left_image": input_tensor(left)[None],
                "right_image": input_tensor(right)[None]})[0]
            if result.shape != (1, 1, self.height, self.width) or not np.isfinite(result).all():
                raise ValueError("Unexpected neural stereo output")
            result = result[0, 0]
            outputs.append(result[:, ::-1].copy() if flip else result)
        return outputs

    def warmup(self):
        y, x = np.mgrid[:self.height, :self.width]
        image = ((x*17+y*31) % 256).astype(np.uint8)
        self.disparity((image, np.roll(image, -8, axis=1)))
