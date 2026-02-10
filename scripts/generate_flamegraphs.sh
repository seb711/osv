#!/bin/bash

# Usage check
if [ $# -lt 1 ]; then
  echo "Usage: $0 <traces_folder> [flamegraph_path]"
  echo "  <traces_folder>: Folder containing the trace files"
  echo "  [flamegraph_path]: Optional path to FlameGraph tools (default: $HOME/Developer/master/tools/FlameGraph)"
  echo "Example: $0 traces /path/to/FlameGraph"
  exit 1
fi

TRACES_FOLDER=$1

# Path to FlameGraph tools - either from argument or default
FLAMEGRAPH_PATH=${2:-"$HOME/Developer/master/tools/FlameGraph"}

# Check if FlameGraph tools exist
if [ ! -f "$FLAMEGRAPH_PATH/flamegraph.pl" ]; then
  echo "Error: FlameGraph tools not found at $FLAMEGRAPH_PATH"
  echo "Please provide the correct path as the second argument"
  exit 1
fi

# Check if traces folder exists
if [ ! -d "$TRACES_FOLDER" ]; then
  echo "Error: Traces folder '$TRACES_FOLDER' not found"
  exit 1
fi

# Find all trace files in the specified folder (exclude .symbols files)
CPU_FILES=$(find "$TRACES_FOLDER" -type f -not -name "*.symbols" -not -name "flamegraph_*.svg")

if [ -z "$CPU_FILES" ]; then
  echo "No trace files found in $TRACES_FOLDER"
  exit 1
fi

FILE_COUNT=$(echo "$CPU_FILES" | wc -w)
echo "Found $FILE_COUNT trace files. Generating flamegraphs..."

# Create a temp directory for intermediate files
TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_DIR"' EXIT

# Process each file and keep track of CPU numbers and intermediate files
CPU_INTERMEDIATE_FILES=()
CPU_NUMBERS=()

for TRACE_FILE in $CPU_FILES; do
  # Extract filename without path
  FILE_NAME=$(basename "$TRACE_FILE")
  
  # Try to extract CPU number from filename if present
  if [[ "$FILE_NAME" =~ cpu([0-9]+) ]]; then
    CPU_NUM="${BASH_REMATCH[1]}"
    OUTPUT_PREFIX="cpu${CPU_NUM}"
    CPU_LABEL="CPU ${CPU_NUM}"
  else
    # Use the filename as prefix if no CPU number is found
    OUTPUT_PREFIX="$FILE_NAME"
    CPU_LABEL="$FILE_NAME"
  fi
  
  echo "Processing $FILE_NAME..."
  
  # Generate intermediate text file in temp directory
  INTERMEDIATE_FILE="${TEMP_DIR}/intermediate_${OUTPUT_PREFIX}.txt"
  ./scripts/trace.py prof-flame -S "$TRACE_FILE" > "$INTERMEDIATE_FILE"
  
  if [ $? -ne 0 ]; then
    echo "Error processing $TRACE_FILE"
    continue
  fi
  
  # Store the intermediate file and CPU label for combined graph later
  CPU_INTERMEDIATE_FILES+=("$INTERMEDIATE_FILE")
  CPU_NUMBERS+=("$CPU_LABEL")
  
  # Generate SVG flamegraph in the same directory as the trace file
  OUTPUT_SVG="${TRACES_FOLDER}/flamegraph_${OUTPUT_PREFIX}.svg"
  "$FLAMEGRAPH_PATH/flamegraph.pl" "$INTERMEDIATE_FILE" > "$OUTPUT_SVG"
  
  if [ $? -eq 0 ]; then
    echo "Generated flamegraph: $OUTPUT_SVG"
  else
    echo "Error generating flamegraph for $FILE_NAME"
  fi
done

# Generate a combined flamegraph from all processed files if there are multiple
if [ $FILE_COUNT -gt 1 ]; then
  echo "Generating labeled combined flamegraph from all processed files..."
  
  COMBINED_SVG="${TRACES_FOLDER}/flamegraph_combined.svg"
  LABELED_COMBINED_SVG="${TRACES_FOLDER}/flamegraph_combined_labeled.svg"
  
  # Create labeled intermediate files for each CPU
  for i in "${!CPU_INTERMEDIATE_FILES[@]}"; do
    CPU_LABEL="${CPU_NUMBERS[$i]}"
    INTERMEDIATE_FILE="${CPU_INTERMEDIATE_FILES[$i]}"
    LABELED_FILE="${TEMP_DIR}/labeled_$(basename "$INTERMEDIATE_FILE")"
    
    # Add CPU label prefix to each line in the intermediate file
    awk -v cpu="$CPU_LABEL;" '{print cpu $0}' "$INTERMEDIATE_FILE" > "$LABELED_FILE"
    CPU_INTERMEDIATE_FILES[$i]="$LABELED_FILE"
  done
  
  # Concatenate all labeled intermediate files
  COMBINED_INTERMEDIATE="${TEMP_DIR}/combined_labeled.txt"
  cat "${CPU_INTERMEDIATE_FILES[@]}" > "$COMBINED_INTERMEDIATE"
  
  # Generate labeled combined SVG with wider width for better visibility
  "$FLAMEGRAPH_PATH/flamegraph.pl" --width 1200 "$COMBINED_INTERMEDIATE" > "$LABELED_COMBINED_SVG"
  
  if [ $? -eq 0 ]; then
    echo "Generated labeled combined flamegraph: $LABELED_COMBINED_SVG"
  else
    echo "Error generating labeled combined flamegraph"
  fi
  
  # Also generate the regular combined flamegraph for comparison
  REGULAR_COMBINED="${TEMP_DIR}/combined_regular.txt"
  for TRACE_FILE in $CPU_FILES; do
    ./scripts/trace.py prof-flame -S "$TRACE_FILE" >> "$REGULAR_COMBINED"
  done
  
  "$FLAMEGRAPH_PATH/flamegraph.pl" "$REGULAR_COMBINED" > "$COMBINED_SVG"
  if [ $? -eq 0 ]; then
    echo "Generated standard combined flamegraph: $COMBINED_SVG"
  fi
fi

echo "Done! All flamegraphs have been generated in the '$TRACES_FOLDER' directory."

# Optional: Open one of the generated SVGs
if [ $FILE_COUNT -gt 0 ]; then
  if command -v xdg-open &> /dev/null; then
    read -p "Do you want to open a flamegraph? (y/n): " OPEN_SVG
    if [[ "$OPEN_SVG" =~ ^[Yy]$ ]]; then
      if [ -f "${TRACES_FOLDER}/flamegraph_combined_labeled.svg" ]; then
        xdg-open "${TRACES_FOLDER}/flamegraph_combined_labeled.svg"
      elif [ -f "${TRACES_FOLDER}/flamegraph_combined.svg" ]; then
        xdg-open "${TRACES_FOLDER}/flamegraph_combined.svg"
      else
        FIRST_SVG=$(find "$TRACES_FOLDER" -name "flamegraph_*.svg" | head -n 1)
        xdg-open "$FIRST_SVG"
      fi
    fi
  fi
fi