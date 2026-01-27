import os
import redis
import argparse

from kr_user_log_redis import UserLogRedis
from kr_user_redis import UserFeatureRedis

def parse_args():
    parser = argparse.ArgumentParser(description='Insert data into redis database')
    parser.add_argument('--path', default='',
                        help='csv file root path')
    parser.add_argument('--dataset', default="KuaiRand-Pure",
                        help="The dataset name (KuaiRand-Pure/KuaiRand-1K/KuaiRand-27K/...)")
    parser.add_argument('-H', '--host', default='localhost',
                        help='Redis host (default: localhost)')
    parser.add_argument('-p', '--port', type=int, default=6379,
                        help='Redis port (default: 6379)')
    parser.add_argument('-d', '--db', type=int, default=0,
                        help='Redis db (default: 0)')
    return parser.parse_args()

def main():
    args = parse_args()
    redis_client = redis.Redis(host=args.host, port=args.port, db=args.db, decode_responses=True)
    ul = UserLogRedis(redis_client)
    uf = UserFeatureRedis(redis_client)

    ul.clear_all()
    uf.clear_all()

    if args.dataset == 'KuaiRand-Pure':
        user_feature_csv = os.path.join(args.path, 'user_features_pure.csv')
        user_log_files = [
            os.path.join(args.path, "log_standard_4_08_to_4_21_pure.csv"),
            os.path.join(args.path, "log_standard_4_22_to_5_08_pure.csv"),
        ]
    elif args.dataset == 'KuaiRand-1K':
        user_feature_csv = os.path.join(args.path, 'user_features_1k.csv')
        user_log_files = [
            os.path.join(args.path, "log_standard_4_08_to_4_21_1k.csv"),
            os.path.join(args.path, "log_standard_4_22_to_5_08_1k.csv"),
        ]
    elif args.dataset == 'KuaiRand-27K':
        user_feature_csv = os.path.join(args.path, 'user_features_27k.csv')
        user_log_files = [
            os.path.join(args.path, "log_standard_4_08_to_4_21_27k_part1.csv"),
            os.path.join(args.path, "log_standard_4_08_to_4_21_27k_part2.csv"),
            os.path.join(args.path, "log_standard_4_22_to_5_08_27k_part1.csv"),
            os.path.join(args.path, "log_standard_4_22_to_5_08_27k_part2.csv"),
        ]
    else:
        raise "dataset should be one of the KuaiRand-Pure/KuaiRand-1K/KuaiRand-27K"

    uf.load_csv(user_feature_csv)
    for user_log in user_log_files:
        ul.load_csv(user_log)

if __name__ == '__main__':
    main()