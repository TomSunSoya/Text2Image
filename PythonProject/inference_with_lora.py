# inference_with_lora.py
import os
import torch
from diffusers import ZImagePipeline
from peft import PeftModel
import test

LOCAL_MODEL_PATH = "C:/Users/pc1/.cache/modelscope/hub/models/Tongyi-MAI/Z-Image-Turbo"
LORA_PATH = "./lora_output/final_lora"

# 加载基础模型
print("加载基础模型...")
pipe = ZImagePipeline.from_pretrained(
    LOCAL_MODEL_PATH,
    torch_dtype=torch.bfloat16,
    local_files_only=True,
)

# 加载 LoRA 权重到 transformer
print("加载 LoRA 权重...")
pipe.transformer = PeftModel.from_pretrained(
    pipe.transformer,
    LORA_PATH,
    torch_dtype=torch.bfloat16
)

pipe = pipe.to("cuda")

# 生成图像
print("生成图像...")
image = pipe(
    prompt=test.prompt3,  # 或者你的自定义 prompt
    num_inference_steps=8,
    guidance_scale=0.0,
    height=768,
    width=768,
).images[0]

# 保存
filename = "lora_output"
i = 1
while os.path.exists(f"{filename}.png"):
    filename = f"lora_output_{i}"
    i += 1

image.save(f"{filename}.png")
print(f"✓ 已保存: {filename}.png")