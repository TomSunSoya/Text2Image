import os

from diffusers import ZImagePipeline
import test
import torch

LOCAL_MODEL_PATH = "C:/Users/pc1/.cache/modelscope/hub/models/Tongyi-MAI/Z-Image-Turbo"

# 加载模型
pipe = ZImagePipeline.from_pretrained(
    LOCAL_MODEL_PATH,
    torch_dtype=torch.bfloat16,
    local_files_only=True,
).to("cuda")

# 生成图像
image = pipe(
    prompt=test.prompt3,
    num_inference_steps=8,
    guidance_scale=0.0,  # Turbo 版本必须为 0
    height=768,
    width=768,
).images[0]

filename = "output1"

i = 1
while os.path.exists(filename + ".png"):
    filename = filename[:-1]
    filename = filename + str(i)
    i += 1

filename = filename + ".png"

# 保存
image.save(filename)