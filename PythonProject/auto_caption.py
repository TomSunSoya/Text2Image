# auto_caption.py
from transformers import Blip2Processor, Blip2ForConditionalGeneration
from PIL import Image
import torch
import os
from tqdm import tqdm


def auto_caption_images(image_folder, output_folder=None):
    """自动为图片生成标注"""
    if output_folder is None:
        output_folder = image_folder

    os.makedirs(output_folder, exist_ok=True)

    # 加载BLIP2模型
    processor = Blip2Processor.from_pretrained("Salesforce/blip2-opt-2.7b")
    model = Blip2ForConditionalGeneration.from_pretrained(
        "Salesforce/blip2-opt-2.7b",
        torch_dtype=torch.float16
    ).to("cuda")

    # 遍历图片
    image_files = [f for f in os.listdir(image_folder)
                   if f.lower().endswith(('.png', '.jpg', '.jpeg', '.webp'))]

    for img_file in tqdm(image_files, desc="标注图片"):
        img_path = os.path.join(image_folder, img_file)
        image = Image.open(img_path).convert('RGB')

        # 生成标注
        inputs = processor(image, return_tensors="pt").to("cuda")
        generated_ids = model.generate(**inputs, max_length=50)
        caption = processor.decode(generated_ids[0], skip_special_tokens=True)

        # 保存为txt文件（与图片同名）
        txt_file = os.path.splitext(img_file)[0] + '.txt'
        txt_path = os.path.join(output_folder, txt_file)
        with open(txt_path, 'w', encoding='utf-8') as f:
            f.write(caption)

        print(f"{img_file} -> {caption}")

    print(f"\n标注完成！共处理 {len(image_files)} 张图片")


# 使用示例
if __name__ == "__main__":
    auto_caption_images("./training_images")