import os
import re
import shutil

new_dir = "combine_dir"
data_dir1 = "data_abtree_zipf"
data_dir2 = "data_abtree_zipf_nocolocated_and_strong_prog"



def find_largest_data_suffix(directory):
    pattern = re.compile(r"^data(\d+)\.txt$")
    max_suffix = -1

    for filename in os.listdir(directory):
        print(filename)
        match = pattern.match(filename)
        if match:
            num = int(match.group(1))
            if num > max_suffix:
                max_suffix = num

    return max_suffix


def copy_txt_files(src_dir, dst_dir):
    # Loop through all items in the source directory
    for filename in os.listdir(src_dir):
        if filename.lower().endswith('.txt'):
            src_path = os.path.join(src_dir, filename)
            dst_path = os.path.join(dst_dir, filename)

            if os.path.isfile(src_path):
                shutil.copy2(src_path, dst_path)  # copy2 preserves metadata

def copy_rename_and_update_step(src_dir, dst_dir, start_index=0):
    filename_pattern = re.compile(r"^data(\d+)\.txt$")
    step_pattern = re.compile(r"^(__step=)(\d+)$")
    txt_files = []

    # Collect matching files
    for filename in os.listdir(src_dir):
        if filename_pattern.match(filename):
            txt_files.append(filename)

    # Sort files based on original numeric suffix
    txt_files.sort(key=lambda f: int(filename_pattern.match(f).group(1)))

    for i, filename in enumerate(txt_files):
        original_match = filename_pattern.match(filename)
        if not original_match:
            continue

        src_path = os.path.join(src_dir, filename)
        new_index = start_index + i
        new_suffix = f"{new_index:06}"
        new_filename = f"data{new_suffix}.txt"
        dst_path = os.path.join(dst_dir, new_filename)

        # Read and update the file content
        with open(src_path, 'r') as f:
            lines = f.readlines()

        updated_lines = []
        for line in lines:
            line = line.rstrip('\n')
            match = step_pattern.match(line)
            if match:
                updated_line = f"{match.group(1)}{new_suffix}"
                updated_lines.append(updated_line)
            else:
                updated_lines.append(line)

        # Write the updated content to the new file
        with open(dst_path, 'w') as f:
            for line in updated_lines:
                f.write(line + '\n')

if __name__ == "__main__":
	data_dir1_last_step = find_largest_data_suffix(data_dir1)
	data_dir2_start_step = data_dir1_last_step + 1
	print(data_dir1_last_step)

	try:
		os.mkdir(new_dir)
		print(f"Directory '{new_dir}' created successfully.")
	except FileExistsError:
		print(f"Directory '{new_dir}' already exists.")
		exit()
	except PermissionError:
		print(f"Permission denied: Unable to create '{new_dir}'.")
		exit()
	except Exception as e:
		print(f"An error occurred: {e}")
		exit()

	copy_txt_files(data_dir1, new_dir)
	copy_rename_and_update_step(data_dir2, new_dir, data_dir2_start_step)
