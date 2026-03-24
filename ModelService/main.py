import os
from pathlib import Path

import torch
from diffusers import ZImagePipeline

LOCAL_MODEL_PATH = os.getenv(
    "MODEL_PATH",
    "C:/Users/pc1/.cache/modelscope/hub/models/Tongyi-MAI/Z-Image-Turbo",
)
DEVICE = "cuda" if torch.cuda.is_available() else "cpu"
DTYPE = torch.bfloat16 if torch.cuda.is_available() else torch.float32
DEFAULT_PROMPT = os.getenv(
    "MODEL_TEST_PROMPT",
    "A cinematic portrait of a traveler in soft evening light",
)
OUTPUT_DIR = Path(os.getenv("MODEL_TEST_OUTPUT_DIR", ".")).resolve()


def next_output_path() -> Path:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    index = 1
    while True:
        candidate = OUTPUT_DIR / f"output{index}.png"
        if not candidate.exists():
            return candidate
        index += 1


def main() -> None:
    pipe = ZImagePipeline.from_pretrained(
        LOCAL_MODEL_PATH,
        torch_dtype=DTYPE,
        local_files_only=True,
    ).to(DEVICE)

    image = pipe(
        prompt=DEFAULT_PROMPT,
        num_inference_steps=8,
        guidance_scale=0.0,
        height=768,
        width=768,
    ).images[0]

    output_path = next_output_path()
    image.save(output_path)
    print(f"saved image to {output_path}")


if __name__ == "__main__":
    main()
