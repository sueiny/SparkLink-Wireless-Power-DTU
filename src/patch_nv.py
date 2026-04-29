file_path = "build/script/nv/generate_data_stream.py"
with open(file_path, "r") as f:
    text = f.read()

text = text.replace("raise ParserError(msg)", "print('ERROR DUMP:', type_name, value); raise ParserError(msg)")

with open(file_path, "w") as f:
    f.write(text)
