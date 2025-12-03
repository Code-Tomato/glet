#!/bin/bash
#
# GPU Scheduler Wrapper Script
#
# Simplified interface for running the standalone scheduler with common configurations.
# Automatically generates sched-config.json based on parameters.
#

set -e

# Default values
RESOURCE_DIR="../resource"
TASK_CONFIG=""
OUTPUT_FILE=""
GPUS=7
PARTS="14,29,43,57,100"
INTERFERENCE=0
VERBOSE=false
GPU_TYPE="a100"

# Usage function
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -t, --task-config FILE    Task configuration CSV file (required)"
    echo "  -o, --output FILE         Output file (default: based on task config)"
    echo "  -g, --gpus N              Number of GPUs (default: 7)"
    echo "  -p, --parts LIST          Partition percentages (default: 14,29,43,57,100)"
    echo "  -i, --interference        Enable interference modeling (default: disabled)"
    echo "  -v, --verbose             Enable verbose output"
    echo "  --gpu-type TYPE           GPU type (default: 3080)"
    echo "  --resource-dir DIR        Resource directory (default: ../resource)"
    echo "  -h, --help                Show this help"
    echo ""
    echo "Examples:"
    echo "  $0 -t 3-model-config.csv -g 2"
    echo "  $0 -t 10-model-config.csv -g 7 -p 50,100 -i"
    echo "  $0 -t 5-model-config.csv -g 5 -v"
    echo ""
    echo "Valid GPU types: a100"
    echo "Valid partitions: 14,29,43,57,100"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--task-config)
            TASK_CONFIG="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        -g|--gpus)
            GPUS="$2"
            shift 2
            ;;
        -p|--parts)
            PARTS="$2"
            shift 2
            ;;
        -i|--interference)
            INTERFERENCE=1
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        --gpu-type)
            GPU_TYPE="$2"
            shift 2
            ;;
        --resource-dir)
            RESOURCE_DIR="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Validate required parameters
if [[ -z "$TASK_CONFIG" ]]; then
    echo "ERROR: Task configuration file is required (-t/--task-config)"
    usage
    exit 1
fi

# Validate task config file exists
if [[ ! -f "$TASK_CONFIG" ]]; then
    echo "ERROR: Task configuration file not found: $TASK_CONFIG"
    exit 1
fi

# Validate GPU type
if [[ "$GPU_TYPE" != "a100" ]]; then
    echo "ERROR: Invalid GPU type: $GPU_TYPE (valid: a100)"
    exit 1
fi

# Generate output filename if not provided
if [[ -z "$OUTPUT_FILE" ]]; then
    BASENAME=$(basename "$TASK_CONFIG" .csv)
    OUTPUT_FILE="${BASENAME}-output.txt"
fi

# Create temporary sched-config.json
TEMP_CONFIG=$(mktemp)
cat > "$TEMP_CONFIG" << EOF
{
    "GPUs": [{"Type": "$GPU_TYPE", "Num": $GPUS}],
    "Max Model": $GPUS,
    "Part": 1,
    "SLO Ratio": 1.1,
    "Latency Ratio": 1.1,
    "Interference": $INTERFERENCE,
    "Avail_Parts": [$(echo $PARTS | tr ',' ' ' | sed 's/ /,/g')],
    "Incremental": 1
}
EOF

echo "=== GPU Scheduler Configuration ==="
echo "Task config: $TASK_CONFIG"
echo "Output file: $OUTPUT_FILE"
echo "GPUs: $GPUS x $GPU_TYPE"
echo "Partitions: $PARTS"
echo "Interference: $([ $INTERFERENCE -eq 1 ] && echo "enabled" || echo "disabled")"
echo "Verbose: $VERBOSE"
echo ""

# Validate task config if validation script exists
if [[ -f "validate_task_config.py" ]]; then
    echo "Validating task configuration..."
    python3 validate_task_config.py "$TASK_CONFIG"
    if [[ $? -ne 0 ]]; then
        echo "Task configuration validation failed!"
        rm -f "$TEMP_CONFIG"
        exit 1
    fi
    echo ""
fi

# Build scheduler command
# Get script directory to find binary
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
SCHEDULER_CMD="$SCRIPT_DIR/../bin/standalone_scheduler"
SCHEDULER_CMD="$SCHEDULER_CMD --resource_dir $RESOURCE_DIR"
SCHEDULER_CMD="$SCHEDULER_CMD --task_config $TASK_CONFIG"
SCHEDULER_CMD="$SCHEDULER_CMD --sched_config $TEMP_CONFIG"
SCHEDULER_CMD="$SCHEDULER_CMD --output $OUTPUT_FILE"
SCHEDULER_CMD="$SCHEDULER_CMD --mem_config $RESOURCE_DIR/mem-config.json"
SCHEDULER_CMD="$SCHEDULER_CMD --device_config $RESOURCE_DIR/device-config.json"

if [[ "$VERBOSE" == "true" ]]; then
    SCHEDULER_CMD="$SCHEDULER_CMD --verbose"
fi

# Run scheduler
echo "Running scheduler..."
echo "Command: $SCHEDULER_CMD"
echo ""

eval $SCHEDULER_CMD
SCHEDULER_EXIT_CODE=$?

# Clean up
rm -f "$TEMP_CONFIG"

# Check result
if [[ $SCHEDULER_EXIT_CODE -eq 0 ]]; then
    echo ""
    echo "Scheduler completed successfully!"
    echo "Results saved to: $OUTPUT_FILE"
    
    # Show summary if output file exists and is not empty
    if [[ -f "$OUTPUT_FILE" && -s "$OUTPUT_FILE" ]]; then
        if [[ "$(head -n1 "$OUTPUT_FILE")" == "EMPTY" ]]; then
            echo "Warning: No valid scheduling found (EMPTY result)"
            echo "   Try: reducing request rates, enabling more partitions, or increasing GPU count"
        else
            echo "Scheduling results:"
            echo "   $(wc -l < "$OUTPUT_FILE") lines in output file"
        fi
    fi
else
    echo ""
    echo "Scheduler failed with exit code $SCHEDULER_EXIT_CODE"
    exit $SCHEDULER_EXIT_CODE
fi
