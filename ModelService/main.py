import argparse
import os
from pathlib import Path

import torch
from diffusers import ZImagePipeline


DEFAULT_MODEL_PATH = os.getenv("MODEL_PATH", "./models/Z-Image-Turbo")
DEFAULT_PROMPT = os.getenv(
    "MODEL_TEST_PROMPT",
    "A cinematic portrait of a traveler in soft evening light",
)
DEFAULT_OUTPUT_DIR = os.getenv("MODEL_TEST_OUTPUT_DIR", "./outputs")


def select_device(preferred: str | None) -> str:
    if preferred:
        return preferred
    return "cuda" if torch.cuda.is_available() else "cpu"


def select_dtype(device: str) -> torch.dtype:
    if device == "cuda":
        if hasattr(torch.cuda, "is_bf16_supported") and torch.cuda.is_bf16_supported():
            return torch.bfloat16
        return torch.float16
    return torch.float32


def build_output_path(output_dir: Path, stem: str) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    candidate = output_dir / f"{stem}.png"
    index = 1
    while candidate.exists():
        candidate = output_dir / f"{stem}_{index}.png"
        index += 1
    return candidate


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate one image with the local Z-Image model")
    parser.add_argument(
        "prompt",
        nargs="?",
        default=DEFAULT_PROMPT,
        help="Prompt text used for generation",
    )
    parser.add_argument("--model-path", default=DEFAULT_MODEL_PATH, help="Local model path")
    parser.add_argument("--device", default=None, help="Execution device, for example cuda or cpu")
    parser.add_argument("--steps", type=int, default=8, help="Number of inference steps")
    parser.add_argument("--height", type=int, default=768, help="Output image height")
    parser.add_argument("--width", type=int, default=768, help="Output image width")
    parser.add_argument("--seed", type=int, default=None, help="Optional random seed")
    parser.add_argument("--output-dir", default=DEFAULT_OUTPUT_DIR, help="Directory for generated image")
    parser.add_argument("--output-name", default="output", help="Base filename without extension")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    device = select_device(args.device)
    dtype = select_dtype(device)

    pipe = ZImagePipeline.from_pretrained(
        args.model_path,
        torch_dtype=dtype,
        local_files_only=True,
    ).to(device)

    generator = None
    if args.seed is not None:
        generator = torch.Generator(device=device).manual_seed(args.seed)

    image = pipe(
        prompt=args.prompt,
        num_inference_steps=args.steps,
        guidance_scale=0.0,
        height=args.height,
        width=args.width,
        generator=generator,
    ).images[0]

    output_path = build_output_path(Path(args.output_dir), args.output_name)
    image.save(output_path)
    print(f"Saved image to {output_path}")


if __name__ == "__main__":
    main()
