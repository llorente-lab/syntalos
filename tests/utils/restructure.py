import os
import shutil
from pathlib import Path

def restructure_directory(source_root):
    """
    Function to restructure a Syntalos dataset

    Args:
    source_root (str): Path to the directory to be restructured

    Returns:
    None (restructures directory)
    """
    dest_root = source_root + "_restructured" # name for our restructured directory
    # let's convert our paths to Pathlib objects so that we can use easy / to navigate and also for easy operations
    source_root = Path(source_root)
    dest_root = Path(dest_root)

    raw_dir = dest_root / "Raw" # create raw files
    analyzed_dir = dest_root / "Analyzed" # create analyzed files (empty for now)

    for date_dir in source_root.iterdir(): # iterate 
        if not date_dir.is_dir(): # break out of loop if the file isn't a directory
            continue

        date_name = date_dir.name # assign date_name to the name of date_dir

        for tp_dir in date_dir.iterdir(): # iterate through tp subdirectory
            if not tp_dir.name.startswith("tp"): # break if it doesnt start with tp
                continue
            
            combined_name = f"{date_name}_{tp_dir.name}" # combine time point and date to datename_tp
            raw_combined_dir = raw_dir / combined_name # create the directories
            analyzed_combined_dir = analyzed_dir / combined_name

            # Create directories here
            (raw_combined_dir / "Miniscope").mkdir(parents=True, exist_ok=True) 
            (raw_combined_dir / "Behavior").mkdir(parents=True, exist_ok=True)

            (analyzed_combined_dir / "Plots").mkdir(parents=True, exist_ok=True)
            (analyzed_combined_dir / "Miniscope" / "Example").mkdir(parents=True, exist_ok=True)
            (analyzed_combined_dir / "Miniscope" / "Plots").mkdir(parents=True, exist_ok=True)
            (analyzed_combined_dir / "Miniscope" / "Files").mkdir(parents=True, exist_ok=True)
            (analyzed_combined_dir / "Behavior" / "Example").mkdir(parents=True, exist_ok=True)
            (analyzed_combined_dir / "Behavior" / "Plots").mkdir(parents=True, exist_ok=True)
            (analyzed_combined_dir / "Behavior" / "Files").mkdir(parents=True, exist_ok=True)

            for root, _, files in os.walk(tp_dir): # now iterate through the Syntalos files
                root_path = Path(root)
                # move around files
                for file in files:
                    source_file = root_path / file
                    if file == 'metadata.json': 
                        shutil.copy2(source_file, raw_combined_dir / file)
                    elif 'miniscope' in root_path.parts:
                        if file.endswith(('.mkv', '.avi', '.tsync', '.csv')):
                            shutil.copy2(source_file, raw_combined_dir / "Miniscope" / file)
                    elif 'orbbec-depth-sensor' in root_path.parts:
                        if file.endswith(('.mkv', '.avi', '.tsync')):
                            shutil.copy2(source_file, raw_combined_dir / "Behavior" / file)

    # we're done!
    print("Directory restructuring complete.")

# if __name__ == "__main__":
    # source_root = "/Users/atharvp04/Downloads/VK_20240827_a"
    # restructure_directory(source_root)
