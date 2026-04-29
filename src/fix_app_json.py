import json

file_path = "middleware/chips/ws63/nv/nv_config/cfg/acore/app.json"
with open(file_path, "r") as f:
    data = json.load(f)

# Update dtu_cfg
dtu_cfg = data["nv_user_config"]["dtu_cfg"]["value"]
# It contains: 
# magic, version, mode, reserved, cfg
# cfg contains: role, uart_cfg, modbus_count, modbus, power, wl_count, whitelist
# But I changed dtu_nv_base_cfg_t to only have: role, uart_cfg, modbus_count, modbus[DTU_NV_MAX_MODBUS_ITEMS], power
# Let's slice the array to exactly what we need
# dtu_cfg[4] is the cfg array
cfg = dtu_cfg[4]
# we only keep role(0), uart_cfg(1), modbus_count(2), modbus(3), power(4)
if len(cfg) > 5:
    dtu_cfg[4] = cfg[:5]

# Update shards
for i in range(8):
    shard_key = f"dtu_wl_shard{i}"
    if shard_key in data["nv_user_config"]:
        shard = data["nv_user_config"][shard_key]["value"]
        # shard: magic, version, shard_index, item_count, items
        items = shard[4]
        for it in range(len(items)):
            # Old item format: mac(0), name_len(1), name(2), uart_cfg(3), modbus_count(4), modbus(5)
            # New format: mac(0), uart_cfg(1), modbus_count(2), modbus(3)
            old_item = items[it]
            if len(old_item) == 6:
                # We need to drop index 1 and 2
                new_item = [
                    old_item[0], # mac
                    old_item[3], # uart_cfg
                    old_item[4], # modbus_count
                    old_item[5]  # modbus
                ]
                items[it] = new_item

with open(file_path, "w") as f:
    json.dump(data, f, indent=4)
