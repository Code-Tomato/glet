#include "latency_model.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <utility>
#include <cassert>
#include <algorithm>
#include <limits>
#include <cmath>

#define MAX_BATCH 32
#define MIN_BATCH 1


void LatencyModel::setupTable(std::string TableFile){
    std::string str_buf;
    std::ifstream file(TableFile);
    std::string line;
    #ifdef DEBUG
    std::cout << __func__ << " called for " << TableFile << std::endl;
    #endif
    while(std::getline(file, line)){
        std::istringstream iss(line);
        std::string field;
        Entry new_entry;
		std::unordered_map<int,float> per_entry_latency;
        std::getline(iss, field,',');
        std::string model=field;
		std::map<std::string,std::unordered_map<int,float>*>::iterator it = _perModelLatnecyTable.find(model);
		if(it == _perModelLatnecyTable.end()){
			_perModelLatnecyTable[model]=new std::unordered_map<int,float>();
            _perModelGPURatioTable[model]=new std::unordered_map<int,float>();
		}
	    std::getline(iss, field,',');
        new_entry.part=stoi(field);
        std::getline(iss, field,',');
        new_entry.batch=stoi(field);
        std::getline(iss, field,',');
        new_entry.latency=stof(field);
	    int key=makeKey(new_entry.batch, new_entry.part);
        _perModelLatnecyTable[model]->operator[](key)=new_entry.latency;
        _perModelBatchVec[model][new_entry.part].push_back(new_entry.batch);      
    } 
}

// acts like an hash key
int LatencyModel::makeKey(int batch, int part){
    return batch*1000 + part;
}

Entry* LatencyModel::parseKey(int key){
    Entry *new_entry  = new Entry();
    new_entry->batch = key/1000;
    new_entry->part =  key % 1000;
    return new_entry;
}

// Helper function to check if a partition exists for a model
bool LatencyModel::hasPartition(std::string model, int part){
    if(_perModelBatchVec.find(model) == _perModelBatchVec.end()){
        return false;
    }
    if(_perModelBatchVec[model].find(part) == _perModelBatchVec[model].end()){
        return false;
    }
    return !_perModelBatchVec[model][part].empty();
}

// Helper function to find the maximum available batch for a partition
int LatencyModel::getMaxAvailableBatch(std::string model, int part){
    if(!hasPartition(model, part)){
        return -1;
    }
    std::vector<int> &batches = _perModelBatchVec[model][part];
    if(batches.empty()){
        return -1;
    }
    return *std::max_element(batches.begin(), batches.end());
}

// Helper function to find the closest available partition
int LatencyModel::findClosestPartition(std::string model, int target_part){
    if(_perModelBatchVec.find(model) == _perModelBatchVec.end()){
        return -1;
    }
    
    int closest_part = -1;
    int min_diff = std::numeric_limits<int>::max();
    
    for(auto &part_pair : _perModelBatchVec[model]){
        if(!part_pair.second.empty()){
            int diff = std::abs(part_pair.first - target_part);
            if(diff < min_diff){
                min_diff = diff;
                closest_part = part_pair.first;
            }
        }
    }
    
    return closest_part;
}

std::pair<int,int> findBatchpair(std::vector<int> &list, int batch, int part)
{
    assert(MIN_BATCH < batch && batch < MAX_BATCH);
    std::pair<int,int> retPair(-1, -1); // Initialize with sentinel values
    
    // Check if list is empty
    if(list.empty()){
        return retPair; // Return sentinel values indicating no batches found
    }
    
    std::vector<int>::iterator it;
    int lowerbatch = batch;
    // Find lower bound with bounds checking
    while(lowerbatch >= MIN_BATCH){
        it=find(list.begin(), list.end(), lowerbatch);
        if(it !=list.end()) {
           retPair.first=lowerbatch;
           break;
        }
        lowerbatch--;
    }
    
    // If no lower batch found, return sentinel
    if(retPair.first == -1){
        return retPair;
    }
    
    int upperbatch = batch;
    // Find upper bound with bounds checking
    while(upperbatch <= MAX_BATCH){
        upperbatch++;
        it=find(list.begin(), list.end(), upperbatch);
        if(it !=list.end()) {
           retPair.second=upperbatch;
           break;
        }
    }
    
    // If no upper batch found, return sentinel
    if(retPair.second == -1){
        return retPair;
    }
    
    return retPair;
}


 float LatencyModel::getBatchPartInterpolatedLatency(std::string model, int batch, int part){
     std::vector<int> keys_vec;
    for(std::unordered_map<int,float>::iterator it = _perModelLatnecyTable[model]->begin();
        it != _perModelLatnecyTable[model]->end();it++ )
    {
        keys_vec.push_back(it->first);
    }
    
    if(keys_vec.size() < 2){
        // Not enough data for interpolation, try direct lookup or return 0
        int key = makeKey(batch, part);
        auto it = _perModelLatnecyTable[model]->find(key);
        if(it != _perModelLatnecyTable[model]->end()){
            return it->second;
        }
        return 0.0;
    }
    
    sort(keys_vec.begin(), keys_vec.end());
    Entry* temp = parseKey(keys_vec.front());
    int min_part = temp->part;
    delete temp;
    temp = parseKey(keys_vec.back());
    int max_part = temp->part;
    delete temp;
    
    // Check if requested partition exists, if not find closest
    int actual_part = part;
    if(!hasPartition(model, part)){
        int closest_part = findClosestPartition(model, part);
        if(closest_part == -1){
            return 0.0;
        }
        actual_part = closest_part;
    }
    
    // Find maximum available batch for each partition (may not be MAX_BATCH)
    int max_batch_min_part = getMaxAvailableBatch(model, min_part);
    int max_batch_actual_part = getMaxAvailableBatch(model, actual_part);
    int max_batch_max_part = getMaxAvailableBatch(model, max_part);
    
    if(max_batch_min_part == -1 || max_batch_actual_part == -1 || max_batch_max_part == -1){
        // Missing data for partitions, fall back to simpler interpolation
        return getBatchInterpolatedLatency(model, batch, actual_part);
    }
    
    // Use the maximum available batch for each partition instead of assuming MAX_BATCH
    float y1 = getBatchInterpolatedLatency(model, max_batch_min_part, min_part);
    float y =  getBatchInterpolatedLatency(model, max_batch_actual_part, actual_part);
    float y2 = getBatchInterpolatedLatency(model, max_batch_max_part, max_part);
    
    if(y1 == 0.0 || y == 0.0 || y2 == 0.0){
        // Fall back to direct interpolation for the requested partition
        return getBatchInterpolatedLatency(model, batch, actual_part);
    }
    
    // Avoid division by zero
    if(std::abs(y1 - y2) < 1e-6){
        return y;
    }
    
    float b = (y - y2) / (y1 - y2);
    y1 = getBatchInterpolatedLatency(model, batch, min_part);
    y2 = getBatchInterpolatedLatency(model, batch, max_part);
    
    if(y1 == 0.0 || y2 == 0.0){
        // Fall back to direct interpolation for the requested partition
        return getBatchInterpolatedLatency(model, batch, actual_part);
    }
    
    float diff = y1 - y2;
    return diff * b + y2;
 }

 float LatencyModel::getBatchInterpolatedLatency(std::string model, int batch, int part){
    // Check if partition exists, if not try to find closest one
    if(!hasPartition(model, part)){
        int closest_part = findClosestPartition(model, part);
        if(closest_part == -1){
            // No partitions available for this model, return 0
            return 0.0;
        }
        part = closest_part; // Use closest partition
    }
    
    // if batch is in the table, lookup and return
    if(batch == MIN_BATCH || batch == MAX_BATCH){
        int key = makeKey(batch, part);
        auto it = _perModelLatnecyTable[model]->find(key);
        if(it != _perModelLatnecyTable[model]->end()){
            return it->second;
        }
        // If exact batch not found, try to find closest available batch
        if(_perModelBatchVec[model][part].empty()){
            return 0.0;
        }
        std::vector<int> &batches = _perModelBatchVec[model][part];
        int closest_batch = batches[0];
        int min_diff = std::abs(batches[0] - batch);
        for(int b : batches){
            int diff = std::abs(b - batch);
            if(diff < min_diff){
                min_diff = diff;
                closest_batch = b;
            }
        }
        int closest_key = makeKey(closest_batch, part);
        auto closest_it = _perModelLatnecyTable[model]->find(closest_key);
        if(closest_it != _perModelLatnecyTable[model]->end()){
            return closest_it->second;
        }
        return 0.0;
    } 
    
    // Check if batch vector is empty for this partition
    if(_perModelBatchVec[model][part].empty()){
        return 0.0;
    }
    
    // if not, do interpolation (batch is between MIN_BATCH and MAX_BATCH)
    std::pair<int,int> two_batch = findBatchpair(_perModelBatchVec[model][part], batch, part);

    // Check if findBatchpair found valid batches
    if(two_batch.first == -1 || two_batch.second == -1){
        // Could not find bounding batches, try to use available data
        std::vector<int> &batches = _perModelBatchVec[model][part];
        if(batches.empty()){
            return 0.0;
        }
        // Use the closest available batch
        int closest_batch = batches[0];
        int min_diff = std::abs(batches[0] - batch);
        for(int b : batches){
            int diff = std::abs(b - batch);
            if(diff < min_diff){
                min_diff = diff;
                closest_batch = b;
            }
        }
        int key = makeKey(closest_batch, part);
        auto it = _perModelLatnecyTable[model]->find(key);
        if(it != _perModelLatnecyTable[model]->end()){
            return it->second;
        }
        return 0.0;
    }

    int b1 = two_batch.first;
    int b2 = two_batch.second;
    int key1 = makeKey(b1, part);
    int key2 = makeKey(b2, part);
    auto it1 = _perModelLatnecyTable[model]->find(key1);
    auto it2 = _perModelLatnecyTable[model]->find(key2);
    
    if(it1 == _perModelLatnecyTable[model]->end() || it2 == _perModelLatnecyTable[model]->end()){
        return 0.0;
    }
    
    float l1 = it1->second;
    float l2 = it2->second;
    
    if(l1 == 0.0 || l2 == 0.0){
        return 0.0;
    }
    
    float ret_latency = (l2-l1)/float(b2-b1) * (batch-b1) + l1;
    return ret_latency;
 }

float LatencyModel::getLatency(std::string model, int batch, int part){
    assert(MIN_BATCH <= batch && batch <= MAX_BATCH);
    // Normalize model names to match latency.csv entries
    if (model == "lenet1" || model == "lenet2" || model == "lenet3" \
    || model == "lenet4" || model == "lenet5" || model=="lenet6"){
        model="lenet1";
    }
    if (model == "ssd-mobilenetv1"){
        model="ssd";
    }
    // if not found, return 0
    if (_perModelLatnecyTable.find(model) == _perModelLatnecyTable.end())
    {
        return 0.0;
    }    
    // try to find part
    int tmp_key = makeKey(batch,part);
    auto it = _perModelLatnecyTable[model]->find(tmp_key);
    if (it == _perModelLatnecyTable[model]->end()){
        //if not found, return interpolated latency
        return getBatchPartInterpolatedLatency(model, batch, part);
    }
    // if found, just use the part 
    return getBatchInterpolatedLatency(model,batch,part);
}


float LatencyModel::getGPURatio(std::string model, int batch, int part){
     assert(MIN_BATCH <= batch && batch <= MAX_BATCH);
    // Normalize model names to match latency.csv entries
    if (model == "lenet1" || model == "lenet2" || model == "lenet3" \
    || model == "lenet4" || model == "lenet5" || model=="lenet6"){
        model="lenet1";
    }
    if (model == "ssd-mobilenetv1"){
        model="ssd";
    }
    
    // Check if model exists
    if(_perModelGPURatioTable.find(model) == _perModelGPURatioTable.end()){
        return 0.0;
    }
    
    // Check if partition exists, if not try to find closest one
    if(!hasPartition(model, part)){
        int closest_part = findClosestPartition(model, part);
        if(closest_part == -1){
            return 0.0;
        }
        part = closest_part;
    }
    
    // if batch is in the table, lookup and return
    if(batch == MIN_BATCH || batch == MAX_BATCH){
        int key = makeKey(batch, part);
        auto it = _perModelGPURatioTable[model]->find(key);
        if(it != _perModelGPURatioTable[model]->end()){
            return it->second;
        }
        // If exact batch not found, try to find closest available batch
        if(_perModelBatchVec[model][part].empty()){
            return 0.0;
        }
        std::vector<int> &batches = _perModelBatchVec[model][part];
        int closest_batch = batches[0];
        int min_diff = std::abs(batches[0] - batch);
        for(int b : batches){
            int diff = std::abs(b - batch);
            if(diff < min_diff){
                min_diff = diff;
                closest_batch = b;
            }
        }
        int closest_key = makeKey(closest_batch, part);
        auto closest_it = _perModelGPURatioTable[model]->find(closest_key);
        if(closest_it != _perModelGPURatioTable[model]->end()){
            return closest_it->second;
        }
        return 0.0;
    } 
    
    // Check if batch vector is empty for this partition
    if(_perModelBatchVec[model][part].empty()){
        return 0.0;
    }
    
    // if not, do interpolation (batch is between MIN_BATCH and MAX_BATCH)
    std::pair<int,int> two_batch = findBatchpair(_perModelBatchVec[model][part], batch, part);
    
    // Check if findBatchpair found valid batches
    if(two_batch.first == -1 || two_batch.second == -1){
        // Could not find bounding batches, try to use available data
        std::vector<int> &batches = _perModelBatchVec[model][part];
        if(batches.empty()){
            return 0.0;
        }
        // Use the closest available batch
        int closest_batch = batches[0];
        int min_diff = std::abs(batches[0] - batch);
        for(int b : batches){
            int diff = std::abs(b - batch);
            if(diff < min_diff){
                min_diff = diff;
                closest_batch = b;
            }
        }
        int key = makeKey(closest_batch, part);
        auto it = _perModelGPURatioTable[model]->find(key);
        if(it != _perModelGPURatioTable[model]->end()){
            return it->second;
        }
        return 0.0;
    }
    
    int b1 = two_batch.first;
    int b2 = two_batch.second;
    int key1 = makeKey(b1, part);
    int key2 = makeKey(b2, part);
    auto it1 = _perModelGPURatioTable[model]->find(key1);
    auto it2 = _perModelGPURatioTable[model]->find(key2);
    
    if(it1 == _perModelGPURatioTable[model]->end() || it2 == _perModelGPURatioTable[model]->end()){
        return 0.0;
    }
    
    float g1 = it1->second;
    float g2 = it2->second;
    
    if(g1 == 0.0 || g2 == 0.0){
        return 0.0;
    }
    
    //2. do linear interpolation and return;
    float ret_gpu_ratio = (g2-g1)/float(b2-b1) * (batch-b1) + g1;
    return ret_gpu_ratio;
}