#!/usr/bin/env python3
import csv
import sys

import redis
from typing import Dict, List, Optional, Union


class UserFeatureRedis:
    def __init__(self, redis_client: redis.Redis, key_prefix: str = "uf"):
        self.r = redis_client
        self.prefix = key_prefix

    def _key(self, user_id: Union[int, str]) -> str:
        return f"{self.prefix}:{user_id}"

    def set(self, user_id: Union[int, str], feat: Dict[str, Union[str, int, float]]) -> bool:
        """单用户特征写入；feat 里可以是 int/float/str，内部统一转 str"""
        mapping = {k: str(v) for k, v in feat.items()}
        return bool(self.r.hset(self._key(user_id), mapping=mapping))

    def set_many(self, items: List[Dict[str, Union[str, int, float]]], id_field: str = "user_id") -> int:
        """批量写入，items 是 List[Dict]，返回写入条数"""
        pipe = self.r.pipeline()
        cnt = 0
        for row in items:
            uid = row[id_field]
            mapping = {k: str(v) for k, v in row.items()}
            pipe.hset(self._key(uid), mapping=mapping)
            cnt += 1
            if cnt % 5000 == 0:
                pipe.execute()
                pipe = self.r.pipeline()
        if pipe:
            pipe.execute()
        return cnt

    def get(self, user_id: Union[int, str]) -> Optional[Dict[str, str]]:
        """返回 Dict[str, str]，空 key 返回 None"""
        raw = self.r.hgetall(self._key(user_id))
        return raw if raw else None

    def get_field(self, user_id: Union[int, str], field: str) -> Optional[str]:
        """取单个字段"""
        return self.r.hget(self._key(user_id), field)

    def exists(self, user_id: Union[int, str]) -> bool:
        return self.r.exists(self._key(user_id))

    def delete(self, user_id: Union[int, str]) -> bool:
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

    def scan_all(self, batch: int = 1000) -> List[Dict[str, str]]:
        """
        游标扫描全部 uf:* 哈希，返回 List[Dict]
        仅百万级可用；数据量大请改用 Redis SCAN + 流式生成器
        """
        keys = self.r.scan(match=f"{self.prefix}:*", count=batch)[1]
        pipe = self.r.pipeline()
        for k in keys:
            pipe.hgetall(k)
        raw = pipe.execute()
        # 把 Redis 返回的 bytes 转 str
        return [dict((k.decode() if isinstance(k, bytes) else k,
                      v.decode() if isinstance(v, bytes) else v) for k, v in d.items()) for d in raw]

    def load_csv(self, csv_path: str, delimiter: str = ",") -> int:
        """一次性导入整个 csv，返回写入条数"""
        cnt = 0
        with open(csv_path, newline='', encoding='utf-8') as f:
            reader = csv.DictReader(f, delimiter=delimiter)
            pipe = self.r.pipeline()
            for row in reader:
                uid = row["user_id"]
                mapping = {k: str(v) for k, v in row.items()}
                pipe.hset(self._key(uid), mapping=mapping)
                cnt += 1
                if cnt % 5000 == 0:
                    pipe.execute()
                    pipe = self.r.pipeline()
            if pipe:
                pipe.execute()
        return cnt



if __name__ == "__main__":
    csv_path =  sys.argv[1]
    print(f"loading csv from {csv_path}")

    r = redis.Redis(host='localhost', port=6379, db=0, decode_responses=True)
    uf = UserFeatureRedis(r, key_prefix="uf")

    cnt = uf.load_csv(csv_path)
    print(f"Total users: {cnt}")

    feat = uf.get(0)
    print(f"[User with id 0] user_active_degree: {feat["user_active_degree"]}, fans_user_num: {feat["fans_user_num"]}")

    print(f"[User with id 1] is_live_streamer: {uf.get_field(25621, 'is_live_streamer')}")
