#include "latency_model.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <utility>
#include <cassert>
#include <algorithm>

#define MAX_BATCH 32
#define MIN_BATCH 1


void LatencyModel::setupTable(std::string TableFile){
    std::string str_buf;
    std::ifstream file(TableFile);
    std::string line;
#ifdef DEBUG
    std::cout << __func__ << " called for " << TableFile << std::endl;
    std::cout << "Setting up the latency file" << std::endl;
#endif
    while(std::getline(file, line)){
#ifdef DEBUG
        std::cout << line << std::endl;
#endif
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


std::pair<int,int> findBatchpair(const std::vector<int> &list, int batch)
{
    // Handle edge case: if list only contains batch=1, return {1, 1} for batch=1
    if (list.size() == 1 && list[0] == 1) {
        if (batch == 1) {
            return std::make_pair(1, 1);
        }
        // If batch > 1 but only batch=1 exists, return {1, -1} to indicate no upper bound
        return std::make_pair(1, -1);
    }

    // If batch is at boundaries and exists in list, return it directly
    if (batch == MIN_BATCH || batch == MAX_BATCH) {
        if (std::find(list.begin(), list.end(), batch) != list.end()) {
            return std::make_pair(batch, batch);
        }
    }

    std::pair<int,int> retPair{-1,-1};

    // find lower neighbor
    int lower = batch;
    while(lower >= MIN_BATCH) {
        if(std::find(list.begin(), list.end(), lower) != list.end()) {
            retPair.first = lower;
            break;
        }
        lower--;
    }

    // find upper neighbor
    int upper = batch;
    while(upper <= MAX_BATCH) {
        if(std::find(list.begin(), list.end(), upper) != list.end()) {
            retPair.second = upper;
            break;
        }
        upper++;
    }

    return retPair;
}


 float LatencyModel::getBatchPartInterpolatedLatency(std::string model, int batch, int part){
    std::vector<int> keys_vec;
    for(std::unordered_map<int,float>::iterator it = _perModelLatnecyTable[model]->begin(); it != _perModelLatnecyTable[model]->end();it++ )
    {
        keys_vec.push_back(it->first);
    }
    assert(keys_vec.size() >= 2);
    sort(keys_vec.begin(), keys_vec.end());
    Entry* temp = parseKey(keys_vec.front());
    int min_part = temp->part;
    delete temp;
    temp = parseKey(keys_vec.back());
    int max_part = temp->part;
    delete temp;
    // assume that every part has max batch size profiled
    int temp_key1 = makeKey(MAX_BATCH,min_part);
    int temp_key2 = makeKey(MAX_BATCH,part);
    int temp_key3 = makeKey(MAX_BATCH,max_part);
    float y1 = getBatchInterpolatedLatency(model,MAX_BATCH,min_part);
    float y =  getBatchInterpolatedLatency(model,MAX_BATCH,part);
    float y2 = getBatchInterpolatedLatency(model,MAX_BATCH,max_part);
    float b=(y-y2)/(y1-y2);
    y1=getBatchInterpolatedLatency(model,batch,min_part);
    y2=getBatchInterpolatedLatency(model,batch,max_part);
    float diff=y1-y2;
    return diff*b + y2;
 }

 float LatencyModel::getBatchInterpolatedLatency(std::string model, int batch, int part){
    uint64_t p1,p2,p3,p4;
    // if batch is in the table, lookup and return
    if(batch == MIN_BATCH || batch == MAX_BATCH){
        return _perModelLatnecyTable[model]->operator[](makeKey(batch,part));
    } 
    
    // Check if only batch=1 exists for this model/partition
    if (onlyHasBatch1(model, part)) {
        // If requesting batch=1, return it directly
        if (batch == 1) {
            return _perModelLatnecyTable[model]->operator[](makeKey(1, part));
        }
        // If requesting batch > 1 but only batch=1 exists, return batch=1 latency
        // This prevents crashes but indicates the model doesn't support larger batches
        return _perModelLatnecyTable[model]->operator[](makeKey(1, part));
    }
    
    // if not, do interpolation
    std::pair<int,int> two_batch = findBatchpair(_perModelBatchVec[model][part], batch);

    int b1 = two_batch.first;
    int b2 = two_batch.second;
    
    // Handle case where we couldn't find neighbors (shouldn't happen after above check, but be safe)
    if (b1 == -1 || b2 == -1) {
        // Fallback: if we have batch=1, use it
        if (std::find(_perModelBatchVec[model][part].begin(), _perModelBatchVec[model][part].end(), 1) != _perModelBatchVec[model][part].end()) {
            return _perModelLatnecyTable[model]->operator[](makeKey(1, part));
        }
        // This should not happen, but return 0.0 as error indicator
        return 0.0;
    }
    
    float l1=_perModelLatnecyTable[model]->operator[](makeKey(b1,part));
    float l2=_perModelLatnecyTable[model]->operator[](makeKey(b2,part));
    
    // Handle case where batch pair is the same (e.g., {1, 1})
    if (b1 == b2) {
        return l1;
    }
    
    assert(l1 != 0.0 && l2 != 0.0);
    float ret_latency = (l2-l1)/float(b2-b1) * (batch-b1) + l1;
    return ret_latency;
 }

float LatencyModel::getLatency(std::string model, int batch, int part){
    assert(MIN_BATCH <= batch && batch <= MAX_BATCH);
    
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
    uint64_t p1,p2,p3,p4;
    // if batch is in the table, lookup and return
    if(batch == MIN_BATCH || batch == MAX_BATCH){
        return _perModelGPURatioTable[model]->operator[](makeKey(batch,part));
    } 
    
    // Check if only batch=1 exists for this model/partition
    if (onlyHasBatch1(model, part)) {
        // If requesting batch=1, return it directly
        if (batch == 1) {
            return _perModelGPURatioTable[model]->operator[](makeKey(1, part));
        }
        // If requesting batch > 1 but only batch=1 exists, return batch=1 ratio
        return _perModelGPURatioTable[model]->operator[](makeKey(1, part));
    }
    
    // if not, do interpolation
    std::pair<int,int> two_batch = findBatchpair(_perModelBatchVec[model][part], batch);
    int b1 = two_batch.first;
    int b2 = two_batch.second;
    
    // Handle case where we couldn't find neighbors
    if (b1 == -1 || b2 == -1) {
        // Fallback: if we have batch=1, use it
        if (std::find(_perModelBatchVec[model][part].begin(), _perModelBatchVec[model][part].end(), 1) != _perModelBatchVec[model][part].end()) {
            return _perModelGPURatioTable[model]->operator[](makeKey(1, part));
        }
        // This should not happen, but return 0.0 as error indicator
        return 0.0;
    }
    
    float g1=_perModelGPURatioTable[model]->operator[](makeKey(b1,part));
    float g2=_perModelGPURatioTable[model]->operator[](makeKey(b2,part));
    
    // Handle case where batch pair is the same (e.g., {1, 1})
    if (b1 == b2) {
        return g1;
    }
    
    assert(g1 != 0.0 && g2 != 0.0);
    //2. do linear interpolation and return;
    float ret_gpu_ratio = (g2-g1)/float(b2-b1) * (batch-b1) + g1;
    return ret_gpu_ratio;
}

bool LatencyModel::onlyHasBatch1(std::string model, int part){
    // Check if model exists in the table
    if (_perModelLatnecyTable.find(model) == _perModelLatnecyTable.end()) {
        return false;
    }
    
    // Check if this partition exists for this model
    if (_perModelBatchVec.find(model) == _perModelBatchVec.end()) {
        return false;
    }
    
    if (_perModelBatchVec[model].find(part) == _perModelBatchVec[model].end()) {
        return false;
    }
    
    // Get the batch vector for this model and partition
    const std::vector<int>& batch_vec = _perModelBatchVec[model][part];
    
    // Check if it only contains batch=1
    return (batch_vec.size() == 1 && batch_vec[0] == 1);
}