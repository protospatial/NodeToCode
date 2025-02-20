import os

def combine_md_files(directory_path, output_file):
    """
    Reads all .md files in the given directory and combines them into a single text file
    with filename tags and double line breaks between files.
    """
    # Create or overwrite the output file
    with open(output_file, 'w', encoding='utf-8') as outfile:
        # Iterate through all files in directory
        for filename in os.listdir(directory_path):
            if filename.endswith('.md'):
                file_path = os.path.join(directory_path, filename)
                # Write filename tag
                outfile.write(f"<{filename}>\n\n")
                
                # Read and write content of the md file
                with open(file_path, 'r', encoding='utf-8') as infile:
                    outfile.write(infile.read())
                    outfile.write('\n\n')  # Add double line break after each file

if __name__ == '__main__':
    # Get current directory as default
    current_dir = os.getcwd()
    output_file = 'combined_md_files.txt'
    
    combine_md_files(current_dir, output_file)
    print(f"Combined MD files have been written to {output_file}")
