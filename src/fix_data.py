import json

file_path = "middleware/chips/ws63/nv/nv_config/cfg/acore/app.json"
with open(file_path, "r") as f:
    data = json.load(f)

# Need to look for any other places with uart_cfg that might have been 0
cfg = data["nv_user_config"]["dtu_cfg"]["value"][4]
print("dtu_cfg cfg array:", cfg)

for i in range(8):
    shard_key = f"dtu_wl_shard{i}"
    if shard_key in data["nv_user_config"]:
        items = data["nv_user_config"][shard_key]["value"][4]
        for it in range(len(items)):
            if items[it][1] == 0:
                print(f"Found shard {i} item {it} with uart_cfg = 0. Fixing...")
                # It expects a dictionary or array representing dtu_nv_uart_cfg_t
                # dtu_nv_uart_cfg_t is: baud_level(0), parity(1), data_bits(2), stop_bits(3)
                items[it][1] = [7, 0, 1, 8] # baud_level=7, parity=0, data_bits=1, stop_bits=8 (just taking defaults from dtu_cfg)

with open(file_path, "w") as f:
    json.dump(data, f, indent=4)
