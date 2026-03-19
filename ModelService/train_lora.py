# train_lora.py
import os
from pathlib import Path
import torch
from torch.utils.data import Dataset, DataLoader
from diffusers import ZImagePipeline
from peft import LoraConfig, get_peft_model
from PIL import Image
from tqdm import tqdm
import torch.nn.functional as F
import numpy as np


DEFAULT_MODEL_PATH = os.getenv("MODEL_PATH", "./models/Z-Image-Turbo")


def select_device() -> str:
    return "cuda" if torch.cuda.is_available() else "cpu"


def select_dtype(device: str) -> torch.dtype:
    if device == "cuda":
        if hasattr(torch.cuda, "is_bf16_supported") and torch.cuda.is_bf16_supported():
            return torch.bfloat16
        return torch.float16
    return torch.float32


class ImageCaptionDataset(Dataset):
    def __init__(self, image_folder, size=768):
        self.image_folder = Path(image_folder)
        self.size = size
        self.data = []

        for ext in ['*.png', '*.jpg', '*.jpeg']:
            for img_file in self.image_folder.glob(ext):
                txt_file = img_file.with_suffix('.txt')
                if txt_file.exists():
                    self.data.append({
                        'image': img_file,
                        'caption': txt_file.read_text(encoding='utf-8').strip()
                    })

        print(f"找到 {len(self.data)} 个图片-文本对")

    def __len__(self):
        return len(self.data)

    def __getitem__(self, idx):
        item = self.data[idx]
        image = Image.open(item['image']).convert('RGB')
        image = image.resize((self.size, self.size))
        return {
            'image': image,
            'caption': item['caption']
        }


def train_lora(
        model_path=DEFAULT_MODEL_PATH,
        data_folder="./training_images",
        output_dir="./lora_output",
        num_epochs=100,
        batch_size=1,
        learning_rate=1e-4,
        lora_rank=16,  # 5070Ti 建议从16开始
        lora_alpha=16,
):
    os.makedirs(output_dir, exist_ok=True)
    device = select_device()
    dtype = select_dtype(device)

    # 1. 加载模型
    print("加载 Z-Image 模型...")
    pipe = ZImagePipeline.from_pretrained(
        model_path,
        torch_dtype=dtype,
        local_files_only=True,
    )

    # 检查模型组件
    print(f"\n模型组件:")
    print(f"- transformer: {type(pipe.transformer).__name__}")
    print(f"- text_encoder: {type(pipe.text_encoder).__name__}")
    print(f"- vae: {type(pipe.vae).__name__}")

    # 2. 配置 LoRA（针对 Transformer）
    print("\n配置 LoRA...")

    # 查找 transformer 中的注意力层模块名
    target_modules = []
    for name, module in pipe.transformer.named_modules():
        if 'attn' in name.lower() and ('to_q' in name or 'to_k' in name or 'to_v' in name or 'to_out' in name):
            module_name = name.split('.')[-1]
            if module_name not in target_modules:
                target_modules.append(module_name)

    # 如果没找到，使用常见的名称
    if not target_modules:
        target_modules = ["to_q", "to_k", "to_v", "to_out"]

    print(f"LoRA 目标模块: {target_modules}")

    lora_config = LoraConfig(
        r=lora_rank,
        lora_alpha=lora_alpha,
        target_modules=target_modules,
        lora_dropout=0.1,
        bias="none",
    )

    # 为 Transformer 添加 LoRA
    pipe.transformer = get_peft_model(pipe.transformer, lora_config)
    pipe.transformer.to(device)
    pipe.text_encoder.to(device)
    pipe.vae.to(device)

    # 打印可训练参数
    pipe.transformer.print_trainable_parameters()

    # 3. 准备数据
    print("\n准备数据集...")
    dataset = ImageCaptionDataset(data_folder)

    if len(dataset) == 0:
        raise ValueError("数据集为空！请确保 training_images 文件夹中有图片和对应的 .txt 文件")

    dataloader = DataLoader(dataset, batch_size=batch_size, shuffle=True, num_workers=0)

    # 4. 优化器
    optimizer = torch.optim.AdamW(
        pipe.transformer.parameters(),
        lr=learning_rate,
        betas=(0.9, 0.999),
        weight_decay=0.01
    )

    # 5. 训练循环
    print("\n开始训练...\n")
    pipe.transformer.train()
    pipe.vae.eval()
    pipe.text_encoder.eval()

    for epoch in range(num_epochs):
        epoch_loss = 0
        progress_bar = tqdm(dataloader, desc=f"Epoch {epoch + 1}/{num_epochs}")

        for step, batch in enumerate(progress_bar):
            try:
                images = batch['image']
                captions = batch['caption']

                # 图像转 tensor
                pixel_values = torch.stack([
                    torch.from_numpy(np.array(img)).permute(2, 0, 1).float() / 127.5 - 1.0
                    for img in images
                ]).to(device, dtype=dtype)

                # VAE 编码
                with torch.no_grad():
                    latents = pipe.vae.encode(pixel_values).latent_dist.sample()
                    latents = latents * pipe.vae.config.scaling_factor

                # 文本编码
                with torch.no_grad():
                    text_inputs = pipe.tokenizer(
                        captions,
                        padding="max_length",
                        max_length=pipe.tokenizer.model_max_length,
                        truncation=True,
                        return_tensors="pt"
                    ).to(device)

                    text_embeddings = pipe.text_encoder(text_inputs.input_ids)[0]

                # 添加噪声
                noise = torch.randn_like(latents)
                bsz = latents.shape[0]
                timesteps = torch.randint(
                    0, pipe.scheduler.config.num_train_timesteps,
                    (bsz,),
                    device=latents.device
                ).long()

                noisy_latents = pipe.scheduler.add_noise(latents, noise, timesteps)

                # Transformer 预测
                model_pred = pipe.transformer(
                    noisy_latents,
                    timestep=timesteps,
                    encoder_hidden_states=text_embeddings,
                ).sample

                # 计算损失
                loss = F.mse_loss(model_pred.float(), noise.float(), reduction="mean")

                # 反向传播
                optimizer.zero_grad()
                loss.backward()
                torch.nn.utils.clip_grad_norm_(pipe.transformer.parameters(), 1.0)
                optimizer.step()

                epoch_loss += loss.item()
                progress_bar.set_postfix({'loss': f'{loss.item():.4f}'})

            except Exception as e:
                print(f"\n步骤 {step} 出错: {e}")
                continue

        avg_loss = epoch_loss / len(dataloader)
        print(f"\nEpoch {epoch + 1} 平均损失: {avg_loss:.4f}")

        # 每 10 个 epoch 保存
        if (epoch + 1) % 10 == 0:
            save_path = os.path.join(output_dir, f"checkpoint-{epoch + 1}")
            pipe.transformer.save_pretrained(save_path)
            print(f"✓ 已保存检查点: {save_path}")

        # 每 20 个 epoch 生成测试图片
        if (epoch + 1) % 20 == 0:
            print("生成测试图片...")
            pipe.transformer.eval()
            with torch.no_grad():
                test_image = pipe(
                    prompt=dataset.data[0]['caption'],
                    num_inference_steps=8,
                    guidance_scale=0.0,
                    height=768,
                    width=768,
                ).images[0]
                test_image.save(os.path.join(output_dir, f"test_epoch_{epoch + 1}.png"))
            pipe.transformer.train()

    # 保存最终模型
    final_path = os.path.join(output_dir, "final_lora")
    pipe.transformer.save_pretrained(final_path)
    print(f"\n✓ 训练完成！LoRA 已保存至: {final_path}")


if __name__ == "__main__":
    train_lora()
