#!/bin/bash
#
# GPU Scheduler Wrapper Script
#
# Simplified interface for running the standalone scheduler.
# Uses sched-config.json from the resource directory.
#

set -e

# Get script directory for relative path resolution
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"

# Default values
RESOURCE_DIR="$PROJECT_ROOT/resource"
TASK_CONFIG=""
OUTPUT_FILE=""
VERBOSE=false

# Usage function
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -t, --task-config FILE    Task configuration CSV file (required)"
    echo "  -o, --output FILE         Output file (default: based on task config)"
    echo "  -v, --verbose             Enable verbose output"
    echo "  --resource-dir DIR        Resource directory (default: resource/)"
    echo "  -h, --help                Show this help"
    echo ""
    echo "Examples:"
    echo "  $0 -t ../resource/SUS-GPUS_INPUT.CSV"
    echo "  $0 -t ../resource/test.CSV -v"
    echo "  $0 -t tasks.csv -o results.txt"
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
        -v|--verbose)
            VERBOSE=true
            shift
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

# Set sched-config to use resource directory file
SCHED_CONFIG="$RESOURCE_DIR/sched-config.json"

# Validate sched-config file exists
if [[ ! -f "$SCHED_CONFIG" ]]; then
    echo "ERROR: Scheduling configuration file not found: $SCHED_CONFIG"
    exit 1
fi

# Generate output filename if not provided
if [[ -z "$OUTPUT_FILE" ]]; then
    BASENAME=$(basename "$TASK_CONFIG" .csv)
    OUTPUT_FILE="${BASENAME}-output.txt"
fi

echo "=== GPU Scheduler Configuration ==="
echo "Task config: $TASK_CONFIG"
echo "Scheduler config: $SCHED_CONFIG"
echo "Output file: $OUTPUT_FILE"
echo "Verbose: $VERBOSE"
echo ""

# Validate task config if validation script exists
if [[ -f "validate_task_config.py" ]]; then
    echo "Validating task configuration..."
    python3 validate_task_config.py "$TASK_CONFIG"
    if [[ $? -ne 0 ]]; then
        echo "Task configuration validation failed!"
        exit 1
    fi
    echo ""
fi

# Build scheduler command
SCHEDULER_CMD="$PROJECT_ROOT/bin/standalone_scheduler"
SCHEDULER_CMD="$SCHEDULER_CMD --resource_dir $RESOURCE_DIR"
SCHEDULER_CMD="$SCHEDULER_CMD --task_config $TASK_CONFIG"
SCHEDULER_CMD="$SCHEDULER_CMD --sched_config $SCHED_CONFIG"
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
