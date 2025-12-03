#!/usr/bin/env python3
"""
Task Configuration Validator

Validates CSV task configuration files for the GPU scheduler.
Checks for valid model IDs, proper formatting, and realistic parameters.
"""

import sys
import csv
import argparse
from collections import Counter

# Valid model IDs and their names (from scheduler_base.h)
MODEL_IDS = {
    0: "lenet1", 1: "lenet2", 2: "lenet3", 3: "lenet4", 4: "lenet5", 5: "lenet6",
    6: "googlenet", 7: "resnet50", 8: "ssd-mobilenetv1", 9: "vgg16",
    10: "mnasnet1_0", 11: "mobilenet_v2", 12: "densenet161", 13: "bert"
}

# Typical SLO ranges (ms) for each model
TYPICAL_SLOS = {
    0: 5, 1: 5, 2: 5, 3: 5, 4: 5, 5: 5,  # lenet variants
    6: 66, 7: 108, 8: 202, 9: 142, 10: 62, 11: 64, 12: 202, 13: 22
}

def validate_csv_file(filename):
    """Validate a task configuration CSV file."""
    errors = []
    warnings = []
    model_ids = []
    
    try:
        with open(filename, 'r') as f:
            lines = f.readlines()
    except FileNotFoundError:
        return [f"ERROR: File '{filename}' not found"], []
    
    if not lines:
        return [f"ERROR: File '{filename}' is empty"], []
    
    # Check first line (number of models)
    try:
        num_models = int(lines[0].strip())
    except ValueError:
        return [f"ERROR: First line must be a number (got: '{lines[0].strip()}')"], []
    
    if num_models <= 0:
        return [f"ERROR: Number of models must be positive (got: {num_models})"], []
    
    if num_models > 20:
        warnings.append(f"WARNING: Large number of models ({num_models}), scheduling may be slow")
    
    # Check data lines
    if len(lines) < num_models + 1:
        return [f"ERROR: Expected {num_models + 1} lines, got {len(lines)}"], []
    
    for i in range(1, num_models + 1):
        line = lines[i].strip()
        line_num = i + 1
        
        # Check for trailing comma
        if not line.endswith(','):
            errors.append(f"ERROR: Line {line_num} missing trailing comma: '{line}'")
            continue
        
        # Parse CSV line
        try:
            parts = line.rstrip(',').split(',')
            if len(parts) != 3:
                errors.append(f"ERROR: Line {line_num} must have 3 fields (got {len(parts)}): '{line}'")
                continue
            
            model_id = int(parts[0])
            request_rate = int(parts[1])
            slo = int(parts[2])
            
        except ValueError as e:
            errors.append(f"ERROR: Line {line_num} has invalid numbers: '{line}' ({e})")
            continue
        
        # Validate model ID
        if model_id not in MODEL_IDS:
            errors.append(f"ERROR: Line {line_num} invalid model ID {model_id} (valid: 0-13)")
            continue
        
        model_ids.append(model_id)
        
        # Validate request rate
        if request_rate <= 0:
            errors.append(f"ERROR: Line {line_num} request rate must be positive (got: {request_rate})")
        elif request_rate > 1000:
            warnings.append(f"WARNING: Line {line_num} high request rate {request_rate} may cause SLO violations")
        
        # Validate SLO
        if slo <= 0:
            errors.append(f"ERROR: Line {line_num} SLO must be positive (got: {slo})")
        elif slo > 1000:
            warnings.append(f"WARNING: Line {line_num} very high SLO {slo}ms may be unrealistic")
        
        # Check for reasonable SLO vs typical
        typical_slo = TYPICAL_SLOS[model_id]
        if slo < typical_slo * 0.5:
            warnings.append(f"WARNING: Line {line_num} SLO {slo}ms is much lower than typical {typical_slo}ms for {MODEL_IDS[model_id]}")
        elif slo > typical_slo * 2:
            warnings.append(f"WARNING: Line {line_num} SLO {slo}ms is much higher than typical {typical_slo}ms for {MODEL_IDS[model_id]}")
    
    # Check for duplicate model IDs
    duplicates = [model_id for model_id, count in Counter(model_ids).items() if count > 1]
    if duplicates:
        errors.append(f"ERROR: Duplicate model IDs found: {duplicates}")
    
    # Check for unused lines
    if len(lines) > num_models + 1:
        warnings.append(f"WARNING: {len(lines) - num_models - 1} extra lines after data")
    
    return errors, warnings

def main():
    parser = argparse.ArgumentParser(description='Validate task configuration CSV files')
    parser.add_argument('filename', help='CSV file to validate')
    parser.add_argument('--show-models', action='store_true', help='Show valid model IDs and names')
    
    args = parser.parse_args()
    
    if args.show_models:
        print("Valid Model IDs:")
        for model_id, name in MODEL_IDS.items():
            print(f"  {model_id}: {name} (typical SLO: {TYPICAL_SLOS[model_id]}ms)")
        return
    
    errors, warnings = validate_csv_file(args.filename)
    
    if errors:
        print("❌ VALIDATION FAILED")
        for error in errors:
            print(f"  {error}")
        sys.exit(1)
    
    if warnings:
        print("⚠️  VALIDATION PASSED WITH WARNINGS")
        for warning in warnings:
            print(f"  {warning}")
    else:
        print("✅ VALIDATION PASSED")
    
    print(f"\nFile: {args.filename}")
    print("Ready for scheduler execution!")

if __name__ == '__main__':
    main()




