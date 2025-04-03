#include <cstdio>
#include <vector>
#include <queue>
#include <cstring>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <climits>
#include <getopt.h> 
#include <iostream>
#include <string>
using namespace std;


const int MAX_VPAGES = 64;   
FILE* input_file = nullptr;
char buffer[256];


const int COST_READ_WRITE = 1;
const int COST_CONTEXT_SWITCH = 130;
const int COST_PROCESS_EXIT = 1230;

const int COST_MAP = 350;
const int COST_UNMAP = 410;
const int COST_IN = 3200;
const int COST_OUT = 2750;
const int COST_FIN = 2350;
const int COST_FOUT = 2800;
const int COST_ZERO = 150;
const int COST_SEGV = 440;
const int COST_SEGPROT = 410;


size_t inst_count = 0;         
size_t ctx_switches = 0;       
size_t process_exits = 0;      
unsigned long long cost = 0;   

struct Frame {
    int pid;    
    int vpage;
    uint32_t age;
    size_t last_reference;
};


struct PTE {
    unsigned int present : 1;
    unsigned int referenced : 1;
    unsigned int modified : 1;
    unsigned int write_protect : 1;
    unsigned int pagedout : 1;
    unsigned int frame : 7;
    unsigned int padding : 20;
};


struct VMA {
    int start_vpage;
    int end_vpage;
    bool write_protected;
    bool file_mapped;
};


struct Process {
    int id;
    vector<VMA> vmas;
    PTE page_table[MAX_VPAGES];

    int unmaps = 0;
    int maps = 0;
    int ins = 0;
    int outs = 0;
    int zeros = 0;
    int segv = 0;
    int segprot = 0;
    int fins=0;
    int fouts=0; 
};

std::vector<Frame> frame_table; 
queue<int> free_list;

vector<int> randvals;  
int ofs = 0; 
void load_random_numbers(const string& filename) {
    ifstream infile(filename);
    if (!infile) {
        exit(EXIT_FAILURE);
    }

    int count;
    infile >> count;
    randvals.resize(count);
    for (int i = 0; i < count; ++i) {
        infile >> randvals[i];
    }
}

int myrandom(int burst) {
    int result = 1 + (randvals[ofs] % burst);  
    ofs = (ofs + 1) % randvals.size();  
    return result;
}

class Pager {
public:
    virtual ~Pager() {}
    virtual Frame* select_victim_frame() = 0; 
};

class FIFOPager : public Pager {
    int hand;
    int MAX_FRAMES; 

public:
    FIFOPager(int num) : hand(0), MAX_FRAMES(num) {}

    Frame* select_victim_frame() override {
        Frame* victim_frame = &(frame_table)[hand];

        hand = (hand + 1) % MAX_FRAMES;

        return victim_frame;
    }
};

class RandomPager : public Pager {
    int MAX_FRAMES;
public:
    RandomPager(int num) : MAX_FRAMES(num) {}
    Frame* select_victim_frame() override {
        int random_index = myrandom(MAX_FRAMES)-1; 
        return &(frame_table)[random_index];
    }
};

class ClockPager : public Pager {
    int hand; 
    vector<Process>& processes; 
    int MAX_FRAMES;

public:
    ClockPager(vector<Process>& procs, int num) : hand(0), processes(procs), MAX_FRAMES(num) {}

    Frame* select_victim_frame() override {
        while (true) {
            Frame& frame = (frame_table)[hand];
            Process& process = processes[frame.pid];
            PTE* pte = &process.page_table[frame.vpage];

            if (pte->referenced == 1) {
                pte->referenced = 0;
                hand = (hand + 1) % MAX_FRAMES;
            } else {
                Frame* victim_frame = &(frame_table)[hand];
                hand = (hand + 1) % MAX_FRAMES;
                return victim_frame;
            }
        }
    }
};


class AgingPager : public Pager {
    int hand;                   
    vector<Process>& processes; 
    int MAX_FRAMES;
public:
    AgingPager(vector<Process>& procs, int num) : hand(0), processes(procs), MAX_FRAMES(num) {}

    Frame* select_victim_frame() override {
        int victim_index = -1;
        uint32_t min_age = UINT32_MAX; 
        int frames_scanned = 0;

        
        for (int i = 0; i < MAX_FRAMES; i++) {
            frames_scanned++;
            int frame_index = (hand + i) % MAX_FRAMES; 
            Frame& frame = (frame_table)[frame_index];
            PTE* pte = &processes[frame.pid].page_table[frame.vpage];

            frame.age = (frame.age >> 1) | (pte->referenced ? 0x80000000 : 0);

            pte->referenced = 0;

            if (frame.age < min_age) {
                min_age = frame.age;
                victim_index = frame_index;
            }
        }

        hand = (victim_index + 1) % MAX_FRAMES;

        return &(frame_table)[victim_index];
    }
};



class WorkingSetPager : public Pager {
    int hand;                  
    vector<Process>& processes; 
    const int tau;              
    int MAX_FRAMES;

public:
    WorkingSetPager(vector<Process>& procs, int tau_window, int num) 
        : hand(0), processes(procs), tau(tau_window), MAX_FRAMES(num) {}

    Frame* select_victim_frame() override {
        int victim_index = -1;         
        int oldest_time = INT_MAX;    
        int frames_scanned = 0;       
        int start_hand = hand;        

        for (int i = 0; i < MAX_FRAMES; i++) {
            frames_scanned++;
            int frame_index = (hand+i) % MAX_FRAMES; 
            Frame& frame = (frame_table)[frame_index];
            PTE* pte = &processes[frame.pid].page_table[frame.vpage];

            int time_since_last_reference = inst_count - frame.last_reference;

            
            if(pte->referenced == 0){
                if (time_since_last_reference > tau) {
                    victim_index = frame_index;
                    hand = (frame_index + 1) % MAX_FRAMES; 
                    return &(frame_table)[frame_index];
                }

                else if (frame.last_reference < oldest_time) {
                    oldest_time = frame.last_reference;
                    victim_index = frame_index;
                }
            }
            else {
                frame.last_reference = inst_count;
                pte->referenced = 0;
            }
            
        }

        if(victim_index == -1){
            victim_index = hand;
        }
        hand = (victim_index + 1) % MAX_FRAMES; 
        return &(frame_table)[victim_index];
    }
};



class ESCNRUPager : public Pager {
    int hand;                   
    vector<Process>& processes; 
    int last_reset_time;
    int MAX_FRAMES;        

public:
    ESCNRUPager(vector<Process>& procs, int num) 
        : hand(0), processes(procs), last_reset_time(0), MAX_FRAMES(num) {}

    Frame* select_victim_frame() override {
        int victim_index = -1;             
        int frames_scanned = 0;            
        bool reset_needed = (inst_count - last_reset_time) >= 48; 
        int lowest_class_found = 4;        
        int class_victims[4] = {-1, -1, -1, -1}; 

        
        for (int i = 0; i < MAX_FRAMES; i++) {
            frames_scanned++;
            int frame_index = (hand + i) % MAX_FRAMES; 
            Frame& frame = (frame_table)[frame_index];
            PTE* pte = &processes[frame.pid].page_table[frame.vpage];

            int frame_class = (pte->referenced << 1) | pte->modified;

            if (class_victims[frame_class] == -1) {
                class_victims[frame_class] = frame_index;
                lowest_class_found = std::min(lowest_class_found, frame_class);
            }

            if (reset_needed) {
                pte->referenced = 0;
            }

            if (frame_class == 0 && !reset_needed) {
                victim_index = frame_index;
                break;
            }
        }

        if (victim_index == -1) {
            victim_index = class_victims[lowest_class_found];
        }

        hand = (victim_index + 1) % MAX_FRAMES;

        if (reset_needed) {
            last_reset_time = inst_count;
        }

        return &(frame_table)[victim_index];
    }
};


Pager* THE_PAGER = nullptr;

void initialize_frame_table(int MAX_FRAMES) {
    frame_table.resize(MAX_FRAMES);
    for (int i = 0; i < MAX_FRAMES; i++) {
        frame_table[i] = {-1, -1, 0, 0};  
        free_list.push(i);         
    }
}

Frame* allocate_frame_from_free_list() {
    if (free_list.empty()) return nullptr;
    int frame_index = free_list.front();
    free_list.pop();
    return &(frame_table)[frame_index];
}

Frame* get_frame() {
    Frame* frame = allocate_frame_from_free_list();
    if (frame != nullptr) {
        return frame; 
    }

    return THE_PAGER->select_victim_frame();
}


const VMA* find_vma_for_page(Process* process, int vpage) {
    for (const auto& vma : process->vmas) {
        if (vpage >= vma.start_vpage && vpage <= vma.end_vpage) {
            return &vma; 
        }
    }
    return nullptr; 
}


bool read_line_func(FILE* file, char* buffer, size_t buffer_size) {
    while (fgets(buffer, buffer_size, file)) {
        if (buffer[0] == '#' || buffer[0] == '\n') continue; 
        return true; 
    }
    return false; 
}

void initialize_page_table(PTE page_table[]) {
    for (int i = 0; i < MAX_VPAGES; i++) {
        page_table[i] = {
            .present = 0,        
            .referenced = 0,     
            .modified = 0,       
            .write_protect = 0,  
            .pagedout = 0,       
            .frame = 0           
        };
    }
}

vector<Process> read_processes(FILE* file) {
    vector<Process> processes;

    if (!read_line_func(file, buffer, sizeof(buffer))) {
        exit(EXIT_FAILURE);
    }

    int num_processes;
    sscanf(buffer, "%d", &num_processes);

    for (int i = 0; i < num_processes; i++) {
        Process process;
        process.id = i;
        initialize_page_table(process.page_table);
        if (!read_line_func(file, buffer, sizeof(buffer))) {
            exit(EXIT_FAILURE);
        }

        int num_vmas;
        sscanf(buffer, "%d", &num_vmas);

        for (int j = 0; j < num_vmas; j++) {

            VMA vma;
            if (!read_line_func(file, buffer, sizeof(buffer))) {
                exit(EXIT_FAILURE);
            }

            int write_protected, file_mapped;
            int fields_read = sscanf(buffer, "%d %d %d %d", 
                                    &vma.start_vpage, &vma.end_vpage, 
                                    &write_protected, &file_mapped);

            if (fields_read != 4) {
                exit(EXIT_FAILURE);
            }

            vma.write_protected = (write_protected != 0);
            vma.file_mapped = (file_mapped != 0);
            process.vmas.push_back(vma);
        }
        processes.push_back(process);
    }

    return processes;
}

bool get_next_instruction(char* operation, int* vpage) {
    if (!input_file) {
        return false;
    }

    while (read_line_func(input_file, buffer, sizeof(buffer))) {
        if (sscanf(buffer, "%c %d", operation, vpage) == 2) {
            return true; 
        }
    }

    return false; 
}

void simulate(vector<Process>& processes, bool option_O) {
    
    char operation;
    int vpage;
    Process* current_process = &processes[0]; 
    int ins_count=0;
    while (get_next_instruction(&operation, &vpage)) {
        if (option_O) printf("%d: ==> %c %d\n",ins_count++, operation, vpage);

        if (operation == 'c') {
            inst_count++;
            ctx_switches++;
            cost += COST_CONTEXT_SWITCH;
            current_process = &processes[vpage];
            continue;
        } else if (operation == 'e') {
            if (option_O) printf("EXIT current process %d\n", current_process->id);
            inst_count++;
            process_exits++;
            cost += COST_PROCESS_EXIT;
            for (int vpage = 0; vpage < MAX_VPAGES; vpage++) {
                PTE* pte = &current_process->page_table[vpage];

                if (pte->present) {
                    if (option_O) printf(" UNMAP %d:%d\n", current_process->id, vpage);
                    current_process->unmaps++;
                    cost += COST_UNMAP;

                    const VMA* vma = find_vma_for_page(current_process, vpage);
                    if (vma && vma->file_mapped && pte->modified) {
                        if (option_O) printf(" FOUT\n");
                        current_process->fouts++;
                        cost += COST_FOUT;
                    }

                    Frame* frame = &(frame_table)[pte->frame];
                    frame->pid = -1;    
                    frame->vpage = -1;  
                    frame->last_reference =0;
                    frame->age = 0;
                    free_list.push(pte->frame);   
                }
                
                pte->present = 0;
                pte->referenced = 0;
                pte->modified = 0;
                pte->write_protect = 0;
                pte->pagedout = 0;
                pte->frame = 0;
            }
            continue;
        }

        inst_count++;
        cost+= COST_READ_WRITE;
        PTE* pte = &current_process->page_table[vpage];
        const VMA* vma = find_vma_for_page(current_process, vpage);
        
        if (!pte->present) {
            if (!vma) {
                cost += COST_SEGV;
                if (option_O) printf(" SEGV\n");
                current_process->segv++;
                continue;
            }

            Frame* new_frame = get_frame();

            if (new_frame->pid != -1) {
                cost += COST_UNMAP;
                Process* old_process = &processes[new_frame->pid];
                PTE* old_pte = &old_process->page_table[new_frame->vpage];
                if (option_O) printf(" UNMAP %d:%d\n", new_frame->pid, new_frame->vpage);
                old_process->unmaps++;
                old_pte->present = 0;

                if (old_pte->modified) {
                const VMA* vma = find_vma_for_page(old_process, new_frame->vpage);
                if (vma && vma->file_mapped) {
                    cost+= COST_FOUT;
                    if (option_O) printf(" FOUT\n");
                    old_process->fouts++;
                } else {
                    cost+= COST_OUT;
                    if (option_O) printf(" OUT\n");
                    old_process->outs++;
                    old_pte->pagedout = 1; 
                }
                }
            }
            
            new_frame->last_reference = inst_count;
            new_frame->age = 0;
            new_frame->pid = current_process->id;
            new_frame->vpage = vpage;

            if (vma->file_mapped) {
                cost += COST_FIN;
                if (option_O) printf(" FIN\n");
                current_process->fins++;
            } else if (pte->pagedout) {
                cost += COST_IN;
                if (option_O) printf(" IN\n");
                current_process->ins++;
            } else {
                cost += COST_ZERO;
                if (option_O) printf(" ZERO\n");
                current_process->zeros++;
            }

            cost += COST_MAP;
            if (option_O) printf(" MAP %ld\n", new_frame - &(frame_table)[0]);
            current_process->maps++;

            pte->frame = new_frame - &(frame_table)[0];
            pte->present = 1;
            pte->referenced = 0;
            pte->modified = 0;
        }
        if (vma->write_protected) pte->write_protect = 1;


        pte->referenced = 1;
        Frame& frame = (frame_table)[pte->frame];

        if (operation == 'w') {
            if (pte->write_protect) {
                current_process->segprot++;
                cost += COST_SEGPROT;
                if (option_O) printf(" SEGPROT\n");
            } else {
                pte->modified = 1;
            }
        }
    }
}

void print_frame_table(int MAX_FRAMES) {
    printf("FT:");
    for (int i = 0; i < MAX_FRAMES; i++) {
        if (frame_table[i].pid == -1) {
            printf(" *");
        } else {
            printf(" %d:%d", frame_table[i].pid, frame_table[i].vpage);
        }
    }
    printf("\n");
}

void print_page_table(vector<Process>& processes) {
    for (const auto& process : processes) {
        printf("PT[%d]:", process.id);
        for (int i = 0; i < MAX_VPAGES; i++) {
            const PTE& pte = process.page_table[i];
            if (!pte.present) {
                if(pte.pagedout){
                printf(" #");
                }
                else
                printf(" *");
            } 
            else {
                printf(" %d:", i);
                if (pte.referenced) printf("R");
                else printf("-");
                if (pte.modified) printf("M");
                else printf("-");
                if (pte.pagedout) printf("S");
                else printf("-");
            }
        }
        printf("\n");
    }
    
}
void print_statistics(vector<Process>& processes) {
    for (const auto& process : processes) {
        printf("PROC[%d]:", process.id);
        printf(" U=%d", process.unmaps);
        printf(" M=%d", process.maps);
        printf(" I=%d", process.ins);
        printf(" O=%d", process.outs);
        printf(" FI=%d", process.fins);
        printf(" FO=%d", process.fouts);
        printf(" Z=%d", process.zeros);
        printf(" SV=%d", process.segv);
        printf(" SP=%d", process.segprot);
        printf("\n");
    }
}

void print_total_cost(){
    printf("TOTALCOST %lu %lu %lu %llu %lu\n", inst_count, ctx_switches, process_exits, cost, sizeof(PTE));
}




int main(int argc, char* argv[]) {
    bool O_option = false, P_option = false, F_option = false, S_option = false;
    bool x_option = false, y_option = false, f_option = false, a_option = false;
    int c;
    opterr = 0;

    int num_frames = 16; 
    char algo_option = 'f'; 
    string options = "";

    while ((c = getopt(argc, argv, "f:a:o:")) != -1) {
        switch (c) {
            case 'f':
                num_frames = atoi(optarg);
                break;
            case 'a':
                algo_option = optarg[0];
                break;
            case 'o':
                options = optarg;
                break;
            default:
                abort();
        }
    }
    

    if (optind + 1 >= argc) {
        return EXIT_FAILURE;
    }

    const char* filename = argv[optind];
    const char* rfile = argv[optind + 1];

    for (char ch : options) {
        switch (ch) {
            case 'O':
                O_option = true;
                break;
            case 'P':
                P_option = true;
                break;
            case 'F':
                F_option = true;
                break;
            case 'S':
                S_option = true;
                break;
            case 'x':
                x_option = true;
                break;
            case 'y':
                y_option = true;
                break;
            case 'f':
                f_option = true;
                break;
            case 'a':
                a_option = true;
                break;
            default:
                return EXIT_FAILURE;
        }
    }


    input_file = fopen(filename, "r");
    vector<Process> processes = read_processes(input_file);
    load_random_numbers(rfile);
    
    initialize_frame_table(num_frames);

    switch (algo_option) {
        case 'f':
            THE_PAGER = new FIFOPager(num_frames);
            break;
        case 'r':
            THE_PAGER = new RandomPager(num_frames);
            break;
        case 'c':
            THE_PAGER = new ClockPager(processes,num_frames);
            break;
        case 'e':
            THE_PAGER = new ESCNRUPager(processes,num_frames);
            break;
        case 'a':
            THE_PAGER = new AgingPager(processes,num_frames);
            break;
        case 'w':
            THE_PAGER = new WorkingSetPager(processes, 49,num_frames); // Default tau = 49
            break;
        default:
            return EXIT_FAILURE;
    }

    simulate(processes, O_option);

    if (P_option) {
        print_page_table(processes);
    }

    if (F_option) {
        print_frame_table(num_frames);
    }

    if (S_option) {
        print_statistics(processes);
        printf("TOTALCOST %lu %lu %lu %llu %lu\n", inst_count, ctx_switches, process_exits, cost, sizeof(PTE));
    }

    fclose(input_file);
    delete THE_PAGER;

    return EXIT_SUCCESS;
}
