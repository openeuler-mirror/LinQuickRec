#!/usr/bin/env python3
import os
import json
import sys

import redis
from typing import List, Dict, Optional
import pandas as pd
from tqdm import tqdm

class UserLogRedis:
    """
    将用户行为日志按 user_id 聚合为 Redis List（有序）
    每行 JSON 格式，字段顺序与 CSV 一致，按 time_ms 升序写入
    key -> ul:{user_id}
    """
    FIELD_NAMES = [
        "user_id", "video_id", "date", "hourmin", "time_ms",
        "is_click", "is_like", "is_follow", "is_comment", "is_forward",
        "is_hate", "long_view", "play_time_ms", "duration_ms",
        "profile_stay_time", "comment_stay_time", "is_profile_enter",
        "is_rand", "tab"
    ]

    def __init__(self, redis_client: redis.Redis, key_prefix: str = "ul"):
        self.r = redis_client
        self.prefix = key_prefix

    def _key(self, user_id: int) -> str:
        return f"{self.prefix}:{user_id}"

    def _serialize(self, row: Dict[str, str]) -> str:
        obj = {f: row[f] for f in self.FIELD_NAMES}
        return json.dumps(obj, separators=(',', ':'))

    def append(self, user_id: int, row: Dict[str, str]) -> int:
        """RPUSH 单条日志，返回当前长度"""
        return self.r.rpush(self._key(user_id), self._serialize(row))

    def extend(self, user_id: int, rows: List[Dict[str, str]]) -> int:
        """批量追加（已排好序）"""
        if not rows:
            return 0
        items = [self._serialize(r) for r in rows]
        return self.r.rpush(self._key(user_id), *items)

    def _serialize(self, row: pd.Series) -> str:
        # 保持字段顺序，直接 Series -> dict -> json
        return json.dumps(row[self.FIELD_NAMES].to_dict(), separators=(',', ':'))

    def load_csv(self, csv_path: str, batch_rows: int = 50_000) -> int:
        df = pd.read_csv(csv_path)
        grouped = df.sort_values("time_ms").groupby("user_id", sort=False)

        total = 0
        pipe = self.r.pipeline()
        for uid, grp in tqdm(grouped, desc="Loading csv"):
            # 一次性拿 5 万条
            for start in range(0, len(grp), batch_rows):
                slice_df = grp.iloc[start:start + batch_rows]
                items = slice_df.apply(self._serialize, axis=1).tolist()
                pipe.rpush(self._key(uid), *items)
                total += len(items)
            if len(pipe) >= batch_rows:
                pipe.execute()
                pipe = self.r.pipeline()
        if pipe:
            pipe.execute()
        return total

    def get_slice(self, user_id: int, start: int = 0, end: int = -1) -> List[Dict]:
        raw = self.r.lrange(self._key(user_id), start, end)
        return [json.loads(item) for item in raw]

    def get_all(self, user_id: int) -> List[Dict]:
        return self.get_slice(user_id, 0, -1)

    def len(self, user_id: int) -> int:
        return self.r.llen(self._key(user_id))

    def pop_left(self, user_id: int) -> Optional[Dict]:
        raw = self.r.lpop(self._key(user_id))
        return json.loads(raw) if raw else None

    def pop_right(self, user_id: int) -> Optional[Dict]:
        raw = self.r.rpop(self._key(user_id))
        return json.loads(raw) if raw else None

    def clear(self, user_id: int) -> bool:
        return bool(self.r.delete(self._key(user_id)))

    def clear_all(self, batch: int = 5000) -> int:
        deleted = 0
        cursor = 0
        while True:
            cursor, keys = self.r.scan(cursor, match=f"{self.prefix}:*", count=batch)
            if keys:
                deleted += self.r.delete(*keys)
            if cursor == 0:
                break
        return deleted


if __name__ == "__main__":
    ''' test redis using KuaiRand-Pure '''
    csv_path =  sys.argv[1]


    r = redis.Redis(host='localhost', port=6379, db=0, decode_responses=True)
    log = UserLogRedis(r)
    log.clear_all()
    cnt = 0
    for csv_file in ['log_standard_4_08_to_4_21_pure.csv', 'log_standard_4_22_to_5_08_pure.csv']:
        cnt += log.load_csv(os.path.join(csv_path, csv_file))
    print(f"Total users: {cnt}")

    logs = log.get_all(0)
    for i, log in enumerate(logs):
        print(f"[User 0]index{i}: {log}")