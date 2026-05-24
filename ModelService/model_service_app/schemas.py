from typing import Optional

from pydantic import BaseModel


class GenerateRequest(BaseModel):
    prompt: str
    negative_prompt: Optional[str] = ""
    num_steps: int = 8
    height: int = 768
    width: int = 768
    seed: Optional[int] = None
    request_id: Optional[str] = None


class GenerateResponse(BaseModel):
    status: str
    request_id: str
    image_url: Optional[str] = None
    message: str
    timestamp: str
    generation_time: Optional[float] = None
