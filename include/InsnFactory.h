// https://github.com/bbiiggppiigg/KernelReorderTool/blob/20514a0c1f932d237a69ea268dcfeec12dce9ba7/lib/InsnFactory.h
#ifndef INSN_FACTORY_H
#define INSN_FACTORY_H
#include <vector>
#include <iostream>
#include <cstring>
using namespace std;

class MyInsn {
	public:
		void * ptr;
		unsigned int size;
		string _pretty;
		MyInsn( void * _ptr , unsigned int _size, string pretty) : ptr(_ptr) , size(_size), _pretty(pretty) {

		};
		void write(FILE* fp){
			fwrite(ptr,size,1,fp);
		}

};

class MyBranchInsn : public MyInsn{
    public:
        
        uint32_t _branch_addr;
        uint32_t _target_addr;
        MyBranchInsn(uint32_t branch_addr, uint32_t target_addr, void * cmd_ptr,uint32_t size, string pretty) : MyInsn ( cmd_ptr, size , pretty), _branch_addr(branch_addr) , _target_addr(target_addr) {
            printf("Creating Branch Insns of size %u\n",size); 
        }

        void update_for_move(uint32_t offset){
            _branch_addr+= offset;
            _target_addr+= offset;
        }
        /*
         * There are two cases, 
         * branch forward, where we increase the target_addr by the insertion_size
         * backward branch
         *
         */
        void update_for_insertion(uint32_t insert_loc, uint32_t insert_size){


            //uint32_t cmd_value = *(uint32_t*) ptr;
            

            if (insert_loc <= _branch_addr )
                _branch_addr += insert_size;
            if (insert_loc < _target_addr)
                _target_addr += insert_size;

            if( _branch_addr < _target_addr ){
                int16_t simm16 = ( (int32_t) _target_addr - 4 - (int32_t) _branch_addr ) >> 2;
                memcpy(ptr,&simm16,2);
            }else{
                int16_t simm16 = ( (int32_t) _target_addr - 4 - (int32_t) _branch_addr ) >> 2;
                memcpy(ptr,&simm16,2);
            }
            //uint32_t new_value = *(uint32_t*) ptr;

        }
		void write_to_file(FILE* fp){
            ////printf("updateing branch instruction at address %x\n",_branch_addr);
            fseek(fp,_branch_addr,SEEK_SET);
			fwrite(ptr,size,1,fp);
		}
};

class InsnFactory {

	public:
        // SOPC
        static MyInsn create_s_cmp_eq_u64( uint32_t ssrc1, uint32_t ssrc0, vector<char *> & insn_pool ){
			uint32_t cmd = 0xbf000000;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			uint32_t op = 18;
			cmd  = ( cmd | (op<< 16) | (ssrc1 << 8) |ssrc0);
			memcpy( cmd_ptr ,&cmd,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,4,std::string("s_cmp_eq_u64 "));
		}

            


        // SOPP
        //
        static MyBranchInsn create_s_cbranch_execz(uint32_t branch_addr, uint32_t target_addr, char * cmd , vector<char *> & insn_pool){
            void * cmd_ptr =  malloc(sizeof(char ) * 4);
            insn_pool.push_back( (char *) cmd_ptr);
            memcpy(cmd_ptr,cmd,4);
            return MyBranchInsn( branch_addr, target_addr,cmd_ptr, 4 , string("s_cbranch_execz"));
        }

        static MyBranchInsn create_s_cbranch_scc1(uint32_t branch_addr, uint32_t target_addr, char * cmd , vector<char *> & insn_pool){
            void * cmd_ptr =  malloc(sizeof(char ) * 4);
            insn_pool.push_back( (char *) cmd_ptr);
            memcpy(cmd_ptr,cmd,4);
            return MyBranchInsn( branch_addr, target_addr,cmd_ptr, 4 , string("s_cbranch_scc1"));
        }


        static MyBranchInsn create_s_cbranch_execnz(uint32_t branch_addr, uint32_t target_addr, char * cmd , vector<char *> & insn_pool){
            void * cmd_ptr =  malloc(sizeof(char ) * 4);
            insn_pool.push_back( (char * ) cmd_ptr);
            memcpy(cmd_ptr,cmd,4);
            return MyBranchInsn( branch_addr, target_addr,cmd_ptr, 4 , string("s_cbranch_execnz"));
        }
        static MyBranchInsn create_s_cbranch_vccz(uint32_t branch_addr, uint32_t target_addr, char * cmd , vector<char *> & insn_pool){
            void * cmd_ptr =  malloc(sizeof(char ) * 4);
            insn_pool.push_back( (char * ) cmd_ptr);
            memcpy(cmd_ptr,cmd,4);
            return MyBranchInsn( branch_addr, target_addr,cmd_ptr, 4 , string("s_cbranch_vccz"));
        }

        static MyBranchInsn create_s_cbranch_vccnz(uint32_t branch_addr, uint32_t target_addr, char * cmd , vector<char *> & insn_pool){
            void * cmd_ptr =  malloc(sizeof(char ) * 4);
            insn_pool.push_back( (char * ) cmd_ptr);
            memcpy(cmd_ptr,cmd,4);
            return MyBranchInsn( branch_addr, target_addr,cmd_ptr, 4 , string("s_cbranch_vccnz"));
        }




        static MyBranchInsn convert_to_branch_insn( uint32_t branch_addr, uint32_t target_addr, char * cmd , uint32_t length , vector<char *> & insn_pool){
            if(length == 4){
                uint32_t * cmd_ptr = (uint32_t * ) cmd;
                uint32_t cmd_value = *cmd_ptr;
                uint32_t and_result = cmd_value & 0xff800000;
                if ( and_result == 0xbf800000){
                    uint32_t op =  ( cmd_value >> 16 ) & 0x7f;
                    switch( op ){
                        case 2:
                            return create_s_branch(branch_addr, target_addr,cmd,insn_pool);

                        case 5: // s_cbranch_scc1

                            return create_s_cbranch_scc1(branch_addr, target_addr, cmd,insn_pool);
                        case 4: // s_cbranch_scc0
                        case 6: // s_cbranch_vccz

                            return create_s_cbranch_vccz(branch_addr, target_addr, cmd,insn_pool);
                        case 7: // s_cbranch_vccnz
                            
                            return create_s_cbranch_vccnz(branch_addr, target_addr, cmd,insn_pool);
                            //printf(" op = %u \n",op);
                            break;
                        case 8: // s_cbranch_execz

                            return create_s_cbranch_execz(branch_addr, target_addr, cmd,insn_pool);
                            puts("execz");
                            break;
                        case 9: // s_cbranch_execnz

                            return create_s_cbranch_execnz(branch_addr, target_addr, cmd,insn_pool);
                            puts("execnz");
                            break;
                        default:
                            puts("unknown op");
                            break;
                    }
                                   
                }else{
                    std::cout << std::hex << cmd_value << " "<<(and_result) <<std::endl;
                }
            }else{
                ////printf("length = %u\n",length);
            }
            printf("exiting because of unsupport branch\n");
            exit(-1);
        }

		// SOP1
		//
		static MyInsn create_s_mov_b32( uint32_t sdst, uint32_t ssrc0, bool useImm ,vector<char *> & insn_pool ){
			uint32_t cmd = 0xbe800000;
			uint32_t op = 0;
			char * cmd_ptr;
            if(useImm){
                cmd_ptr = (char * ) malloc(sizeof(char ) * 8);    
                cmd  = ( cmd | (sdst<< 16) | (op << 8) | 255);
			    memcpy( cmd_ptr ,&cmd,  4 );
			    memcpy( cmd_ptr+4 ,&ssrc0,  4 );
			    insn_pool.push_back(cmd_ptr);
			    return MyInsn(cmd_ptr,8,std::string("s_mov_b32 "));
            }else{
                cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			    cmd  = ( cmd | (sdst<< 16) | (op << 8) |ssrc0);
			    memcpy( cmd_ptr ,&cmd,  4 );
			    insn_pool.push_back(cmd_ptr);
			    return MyInsn(cmd_ptr,4,std::string("s_mov_b64 "));
            }
		}


		static MyInsn create_s_mov_b64( uint32_t sdst, uint32_t ssrc0, vector<char *> & insn_pool ){
			uint32_t cmd = 0xbe800000;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			uint32_t op = 1;
			cmd  = ( cmd | (sdst<< 16) | (op << 8) |ssrc0);
			memcpy( cmd_ptr ,&cmd,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,4,std::string("s_mov_b64 "));
		}


		//
		static MyInsn create_s_nop( vector<char *> & insn_pool ){
			uint32_t cmd = 0xbf800000;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			memcpy( cmd_ptr ,&cmd,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,4,std::string("s_nop "));
		}


		// 
		static MyInsn create_s_wait_cnt( vector<char *> & insn_pool ){
			uint32_t cmd = 0xbf8c0000;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			memcpy( cmd_ptr ,&cmd,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,4,std::string("s_waitcnt_0 "));
		}
		// VOP1
		static MyInsn create_v_readfirstlane_b32(  uint32_t vdst, uint32_t src  , vector<char *> & insn_pool ){
			uint32_t cmd = 0x7e000000;
			uint32_t op = 2;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			cmd = ( cmd | (vdst << 17) | ( op << 9)  | src);
			memcpy( cmd_ptr ,&cmd,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,4,std::string("v_mov_b32 "));
		}

		static MyInsn create_v_mov_b32(  uint32_t vdst, uint32_t src  , vector<char *> & insn_pool ){
			uint32_t cmd = 0x7e000000;
			uint32_t op = 1;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			cmd = ( cmd | (vdst << 17) | ( op << 9)  | (src | 0x100));
			memcpy( cmd_ptr ,&cmd,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,4,std::string("v_mov_b32 "));
		}
		static MyInsn create_v_mov_b32_const(  uint32_t vdst, uint32_t src  , vector<char *> & insn_pool ){
			uint32_t cmd = 0x7e000000;
			uint32_t op = 2;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			cmd = ( cmd | (vdst << 17) | ( op << 9)  | 255);
			memcpy( cmd_ptr ,&cmd,  4 );
			memcpy( cmd_ptr+4 ,&src,  4 );
			insn_pool.push_back(cmd_ptr);
            ////printf("Creating v_mob_b32_const instr, cmd = %lx\n",*(uint64_t *)cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("v_mov_b32 "));
		}


		// SOPK
		static MyInsn create_s_getreg_b32(  uint32_t target_sgpr, uint8_t size, uint8_t offset, uint8_t hwRegId , vector<char *> & insn_pool ){
			uint32_t cmd = 0xb0000000;
			uint32_t op = 17;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			size -=1;
			cmd = ( cmd | (op << 23) |  (target_sgpr << 16 ) |(size << 11) | ( offset << 6)  | hwRegId );
			memcpy( cmd_ptr ,&cmd,  4 );
			insn_pool.push_back(cmd_ptr);
			////printf("creating instruction s_getreg_b32 = %x\n",cmd);
			return MyInsn(cmd_ptr,4,std::string("s_getreg ")+std::to_string(hwRegId));
		}

		static MyInsn create_s_movk_i32(  uint32_t target_sgpr, int16_t simm16  , vector<char *> & insn_pool ){
			uint32_t cmd = 0xb0000000;
			uint32_t op = 0x0;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			cmd = ( cmd | (op << 23) | (target_sgpr << 16)  | simm16 );
			memcpy( cmd_ptr ,&cmd,  4 );
			////printf("creating s_movk_i32 s[%u] = %d, cmd = 0x%x\n",target_sgpr,simm16,cmd); 
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,4,std::string("s_movk_i32 ")+std::to_string(simm16));
		}

		// VOP2
		static MyInsn create_v_add_u32(  uint32_t vdst, uint32_t vsrc1, uint32_t src0, vector<char *> & insn_pool ){
			uint32_t cmd = 0x0;
			uint32_t op = 52;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			cmd = ( cmd | (op << 25) | ( vdst << 17) | (vsrc1 << 9)  | src0 );
			memcpy( cmd_ptr ,&cmd,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,4,std::string("v_add_u32 "));
		}
		static MyInsn create_v_add_co_u32(  uint32_t vdst, uint32_t vsrc1, uint32_t src0, vector<char *> & insn_pool ){
			uint32_t cmd = 0x0;
			uint32_t op = 25;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			cmd = ( cmd | (op << 25) | ( vdst << 17) | (vsrc1 << 9)  | src0 );
			memcpy( cmd_ptr ,&cmd,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,4,std::string("v_add_co_u32 "));
		}


		static MyInsn create_v_addc_co_u32(  uint32_t vdst, uint32_t vsrc1, uint32_t src0, vector<char *> & insn_pool ){
			uint32_t cmd = 0x0;
			uint32_t op = 28;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			cmd = ( cmd | (op << 25) | ( vdst << 17) | (vsrc1 << 9)  | src0 );
			memcpy( cmd_ptr ,&cmd,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,4,std::string("v_addc_co_u32 "));
		}


		// VOP3AB
		static MyInsn create_v_mul_lo_u32(  uint32_t vdst, uint32_t src1, uint32_t src0, vector<char *> & insn_pool ){

			uint32_t cmd_low = 0xd0000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 645;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			cmd_low = ( cmd_low | (op << 16)   | vdst );
			cmd_high = ( cmd_high | (src1 << 9)  | src0 );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr+4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("s_and_b32 "));
		}


		// SOP2
        static MyInsn create_s_and_b32(  uint32_t sdst, uint32_t ssrc0, uint32_t ssrc1  , bool useImm , vector<char *> & insn_pool ){
			uint32_t cmd = 0x80000000;
			uint32_t op = 12;

			if(useImm){
				char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
				cmd = ( cmd | (op << 23) | ( sdst << 16) | (ssrc1 << 8)  | 0xff );
				memcpy( cmd_ptr ,&cmd,  4 );
				memcpy( cmd_ptr+4 ,&ssrc0,  4 );
				insn_pool.push_back(cmd_ptr);
				return MyInsn(cmd_ptr,8,std::string("s_add_u32 "));


			}else{
				char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
				cmd = ( cmd | (op << 23) | ( sdst << 16) | (ssrc1 << 8)  | ssrc0 );
				memcpy( cmd_ptr ,&cmd,  4 );
				insn_pool.push_back(cmd_ptr);
				return MyInsn(cmd_ptr,4,std::string("s_sub_u32 "));
			}
		}



        //
		static MyInsn create_s_ashr_i32(  uint32_t sdst, uint32_t ssrc1, uint32_t ssrc0, vector<char *> & insn_pool ){
			uint32_t cmd = 0x80000000;
			uint32_t op = 32;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			cmd = ( cmd | (op << 23) | ( sdst << 16) | (ssrc1 << 8)  | ssrc0 );
			memcpy( cmd_ptr ,&cmd,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,4,std::string("s_and_b32 "));
		}

		static MyInsn create_s_lshr_b32(  uint32_t sdst, uint32_t ssrc1, uint32_t ssrc0, vector<char *> & insn_pool ){
			uint32_t cmd = 0x80000000;
			uint32_t op = 30;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			cmd = ( cmd | (op << 23) | ( sdst << 16) | (ssrc1 << 8)  | ssrc0 );
			memcpy( cmd_ptr ,&cmd,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,4,std::string("s_and_b32 "));
		}
		static MyInsn create_s_lshl_b32(  uint32_t sdst, uint32_t ssrc1, uint32_t ssrc0, vector<char *> & insn_pool ){
			uint32_t cmd = 0x80000000;
			uint32_t op = 28;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			cmd = ( cmd | (op << 23) | ( sdst << 16) | (ssrc1 << 8)  | ssrc0 );
			memcpy( cmd_ptr ,&cmd,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,4,std::string("s_and_b32 "));
		}
		static MyInsn create_s_lshl_b64(  uint32_t sdst, uint32_t ssrc1, uint32_t ssrc0, vector<char *> & insn_pool ){
			uint32_t cmd = 0x80000000;
			uint32_t op = 29;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			cmd = ( cmd | (op << 23) | ( sdst << 16) | (ssrc1 << 8)  | ssrc0 );
			memcpy( cmd_ptr ,&cmd,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,4,std::string("s_and_b32 "));
		}

		static MyInsn create_s_mul_i32(  uint8_t sdst, uint8_t ssrc1, uint8_t ssrc0, vector<char *> & insn_pool ){
			uint32_t cmd = 0x80000000;
			uint32_t op = 36;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			cmd = ( cmd | (op << 23) | ( sdst << 16) | (ssrc1 << 8)  | ssrc0 );
			memcpy( cmd_ptr ,&cmd,  4 );
			insn_pool.push_back(cmd_ptr);
            //printf("generated s_mul_i32 instruction, cmd = 0x%x\n",cmd);
            //printf("dst = %u, ssrc1 = %u, ssrc0 = %u\n",sdst,ssrc1,ssrc0);
			return MyInsn(cmd_ptr,4,std::string("s_and_b32 "));
		}
		static MyInsn create_s_mul_i32_const(  uint8_t sdst, uint8_t ssrc1, uint32_t ssrc0, vector<char *> & insn_pool ){
			uint32_t cmd = 0x80000000;
			uint32_t op = 36;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			cmd = ( cmd | (op << 23) | ( sdst << 16) | (ssrc1 << 8)  | 255 );
            uint32_t cmd_high = ssrc0;

			memcpy( cmd_ptr ,&cmd,  4 );
			memcpy( cmd_ptr+4 ,&cmd_high,  4 );

			insn_pool.push_back(cmd_ptr);
            //printf("generated s_mul_i32_const instruction, cmd = 0x%x:%x\n",cmd,cmd_high);
            //printf("dst = %u, ssrc1 = %u, ssrc0 = %u\n",sdst,ssrc1,ssrc0);
			return MyInsn(cmd_ptr,8,std::string("s_and_b32 "));
		}


		static MyInsn create_s_mul_hi_u32(  uint32_t sdst, uint32_t ssrc1, uint32_t ssrc0, vector<char *> & insn_pool ){
			uint32_t cmd = 0x80000000;
			uint32_t op = 44;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			cmd = ( cmd | (op << 23) | ( sdst << 16) | (ssrc1 << 8)  | ssrc0 );
			memcpy( cmd_ptr ,&cmd,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,4,std::string("s_and_b32 "));
		}

		static MyInsn create_s_and_b32(  uint32_t sdst, uint32_t ssrc1, uint32_t ssrc0, vector<char *> & insn_pool ){
			uint32_t cmd = 0x80000000;
			uint32_t op = 12;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			cmd = ( cmd | (op << 23) | ( sdst << 16) | (ssrc1 << 8)  | ssrc0 );
			memcpy( cmd_ptr ,&cmd,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,4,std::string("s_and_b32 "));
		}
		static MyInsn create_s_and_b32(  uint32_t sdst, uint32_t ssrc1, uint32_t ssrc0, uint32_t simm32 ,vector<char *> & insn_pool ){
			uint32_t cmd = 0x80000000;
			uint32_t op = 12;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			cmd = ( cmd | (op << 23) | ( sdst << 16) | (ssrc1 << 8)  | ssrc0 );
			//printf("cmd_ptr = %llx\n", *(unsigned long long int * ) cmd_ptr+4);
			memcpy( cmd_ptr ,&cmd,  4 );
			//printf("cmd_ptr = %llx\n", *(unsigned long long int * ) cmd_ptr+4);
			memcpy( cmd_ptr+4 ,&simm32,  4 );
			//printf("cmd_ptr = %llx\n", *(unsigned long long int * ) cmd_ptr+4);
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("s_and_b32 "));
		}




		static MyInsn create_s_add_u32(  uint32_t sdst, uint32_t ssrc1, uint32_t ssrc0  , bool useImm , vector<char *> & insn_pool ){
			uint32_t cmd = 0x80000000;
			uint32_t op = 0x0;

			if(useImm){
				char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
				cmd = ( cmd | (op << 23) | ( sdst << 16) | (ssrc1 << 8)  | 0xff );
				memcpy( cmd_ptr ,&cmd,  4 );
				memcpy( cmd_ptr+4 ,&ssrc0,  4 );
				insn_pool.push_back(cmd_ptr);
				return MyInsn(cmd_ptr,8,std::string("s_add_u32 "));


			}else{
				char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
				cmd = ( cmd | (op << 23) | ( sdst << 16) | (ssrc1 << 8)  | ssrc0 );
				memcpy( cmd_ptr ,&cmd,  4 );
				insn_pool.push_back(cmd_ptr);
				return MyInsn(cmd_ptr,4,std::string("s_add_u32 "));
			}
		}


		static MyInsn create_s_addc_u32(  uint32_t sdst, uint32_t ssrc1, uint32_t ssrc0  , bool useImm , vector<char *> & insn_pool ){
			uint32_t cmd = 0x80000000;
			uint32_t op = 0x4;

			if(useImm){
				char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
				cmd = ( cmd | (op << 23) | ( sdst << 16) | (ssrc1 << 8)  | 0xff );
				memcpy( cmd_ptr ,&cmd,  4 );
				memcpy( cmd_ptr+4 ,&ssrc0,  4 );
				insn_pool.push_back(cmd_ptr);
				return MyInsn(cmd_ptr,8,std::string("s_add_u32 "));


			}else{
				char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
				cmd = ( cmd | (op << 23) | ( sdst << 16) | (ssrc1 << 8)  | ssrc0 );
				memcpy( cmd_ptr ,&cmd,  4 );
				insn_pool.push_back(cmd_ptr);
				return MyInsn(cmd_ptr,4,std::string("s_add_u32 "));
			}
		}
		static MyInsn create_s_sub_u32(  uint32_t sdst, uint32_t ssrc0, uint32_t ssrc1  , bool useImm , vector<char *> & insn_pool ){
			uint32_t cmd = 0x80000000;
			uint32_t op = 0x1;

			if(useImm){
				char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
				cmd = ( cmd | (op << 23) | ( sdst << 16) | (ssrc1 << 8)  | 0xff );
				memcpy( cmd_ptr ,&cmd,  4 );
				memcpy( cmd_ptr+4 ,&ssrc0,  4 );
				insn_pool.push_back(cmd_ptr);
				return MyInsn(cmd_ptr,8,std::string("s_add_u32 "));


			}else{
				char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
				cmd = ( cmd | (op << 23) | ( sdst << 16) | (ssrc1 << 8)  | ssrc0 );
				memcpy( cmd_ptr ,&cmd,  4 );
				insn_pool.push_back(cmd_ptr);
				return MyInsn(cmd_ptr,4,std::string("s_sub_u32 "));
			}
		}


		static MyInsn create_s_subb_u32(  uint32_t sdst, uint32_t ssrc0, uint32_t ssrc1  , bool useImm , vector<char *> & insn_pool ){
			uint32_t cmd = 0x80000000;
			uint32_t op = 0x5;

			if(useImm){
				char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
				cmd = ( cmd | (op << 23) | ( sdst << 16) | (ssrc1 << 8)  | 0xff );
				memcpy( cmd_ptr ,&cmd,  4 );
				memcpy( cmd_ptr+4 ,&ssrc0,  4 );
				insn_pool.push_back(cmd_ptr);
				return MyInsn(cmd_ptr,8,std::string("s_subb_u32 "));


			}else{
				char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
				cmd = ( cmd | (op << 23) | ( sdst << 16) | (ssrc1 << 8)  | ssrc0 );
				memcpy( cmd_ptr ,&cmd,  4 );
				insn_pool.push_back(cmd_ptr);
				return MyInsn(cmd_ptr,4,std::string("s_add_u32 "));
			}
		}



		//
		static MyBranchInsn create_s_branch( uint32_t branch_addr , uint32_t target_addr , char * cmd_in  , vector<char *> & insn_pool ){

			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 4 );
			insn_pool.push_back(cmd_ptr);
            if(cmd_in){
                memcpy(cmd_ptr,cmd_in,4); 
            }else{
			    uint32_t cmd = 0xbf800000;
			    uint32_t op = 0x2;
			    uint16_t simm16 = (( target_addr - branch_addr  - 4 )  / 4)  & 0xffff;
			    cmd = ( cmd | (op << 16) | simm16 );
			    memcpy( cmd_ptr ,&cmd,  4 );
			    //printf("creating s_branch from %x to %x, cmd = 0x%x\n",branch_addr,target_addr,cmd); 
            }
			return MyBranchInsn(branch_addr, target_addr , cmd_ptr,4,std::string("s_branch "));
		}
		static MyInsn create_s_memtime( uint32_t sgpr_pair  , vector<char *> & insn_pool ){
			uint32_t cmd_low = 0xc0000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 0x24;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			cmd_low = ( cmd_low | (op << 18) | sgpr_pair << 6);
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			//printf("creating s_memtime sgpr%d 0x%x%x\n",sgpr_pair,cmd_low,cmd_high); 
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("s_memtime ")+std::to_string(sgpr_pair));
		}
		static MyInsn create_s_atomic_add( uint32_t sgpr_target_pair, uint32_t sgpr_addr_pair  ,  uint32_t offset,vector<char *> & insn_pool ){
			uint32_t cmd_low = 0xc0000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 130;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			uint32_t imm = 1;
			cmd_low = ( cmd_low | (op << 18) | (imm << 17) |   sgpr_target_pair << 6 | (sgpr_addr_pair >> 1) );

			cmd_high = ( cmd_high | offset );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("s_load_dword")+std::to_string(sgpr_target_pair));
		}
		static MyInsn create_s_atomic_inc( uint32_t sgpr_target_pair, uint32_t sgpr_addr_pair  ,  uint32_t offset,vector<char *> & insn_pool ){
			uint32_t cmd_low = 0xc0000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 139;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			uint32_t imm = 1;
			cmd_low = ( cmd_low | (op << 18) | (imm << 17) |   sgpr_target_pair << 6 | (sgpr_addr_pair >> 1) );

			cmd_high = ( cmd_high | offset );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("s_load_dword")+std::to_string(sgpr_target_pair));
		}

		static MyInsn create_s_load_dword( uint32_t sgpr_target_pair, uint32_t sgpr_addr_pair  ,  uint32_t offset,vector<char *> & insn_pool ){
			uint32_t cmd_low = 0xc0000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 0x0;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			uint32_t imm = 1;
			cmd_low = ( cmd_low | (op << 18) | (imm << 17) |   sgpr_target_pair << 6 | (sgpr_addr_pair >> 1) );

			cmd_high = ( cmd_high | offset );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("s_load_dword")+std::to_string(sgpr_target_pair));
		}
		static MyInsn create_s_load_dwordx2( uint32_t sgpr_target_pair, uint32_t sgpr_addr_pair  ,  uint32_t offset,vector<char *> & insn_pool ){
			uint32_t cmd_low = 0xc0000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 0x1;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			uint32_t imm = 1;
			cmd_low = ( cmd_low | (op << 18) | (imm << 17) |   sgpr_target_pair << 6 | (sgpr_addr_pair >> 1) );

			cmd_high = ( cmd_high | offset );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("s_load_dwordx2")+std::to_string(sgpr_target_pair));
		}

		static MyInsn create_s_load_dwordx4( uint32_t sgpr_target_pair, uint32_t sgpr_addr_pair  ,  uint32_t offset,vector<char *> & insn_pool ){
			uint32_t cmd_low = 0xc0000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 0x2;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			uint32_t imm = 1;
			cmd_low = ( cmd_low | (op << 18) | (imm << 17) |   sgpr_target_pair << 6 | (sgpr_addr_pair >> 1) );

			cmd_high = ( cmd_high | offset );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("s_load_dwordx4 ")+std::to_string(sgpr_target_pair));
		}
		static MyInsn create_s_load_dwordx8( uint32_t sgpr_target_pair, uint32_t sgpr_addr_pair  ,  uint32_t offset,vector<char *> & insn_pool ){
			uint32_t cmd_low = 0xc0000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 0x3;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			uint32_t imm = 1;
			cmd_low = ( cmd_low | (op << 18) | (imm << 17) |   sgpr_target_pair << 6 | (sgpr_addr_pair >> 1) );

			cmd_high = ( cmd_high | offset );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("s_load_dwordx8 ")+std::to_string(sgpr_target_pair));
		}

		static MyInsn create_s_store_dword_x4( uint32_t sgpr_target_pair, uint32_t sgpr_addr_pair  ,  uint32_t offset,vector<char *> & insn_pool ){
			uint32_t cmd_low = 0xc0000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 18;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			uint32_t imm = 1;
			cmd_low = ( cmd_low | (op << 18) | (imm << 17) |   sgpr_target_pair << 6 | (sgpr_addr_pair >> 1) );

			cmd_high = ( cmd_high | offset );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("s_load_dwordx4 ")+std::to_string(sgpr_target_pair));
		}

		static MyInsn create_s_store_dword_x2( uint32_t s_data_pair, uint32_t s_base_pair  ,  uint32_t offset,vector<char *> & insn_pool ){
			uint32_t cmd_low = 0xc0000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 17;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			uint32_t imm = 1;
			cmd_low = ( cmd_low | (op << 18) | (imm << 17) |   s_data_pair << 6 | (s_base_pair >> 1) );

			cmd_high = ( cmd_high | offset );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("s_store_dword_x2 "));
		}
		static MyInsn create_global_store_dword_x2( uint32_t s_data_pair, uint32_t s_base_pair  ,  uint32_t offset,vector<char *> & insn_pool ){
			uint32_t cmd_low = 0xdc030000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 29;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			cmd_low = (cmd_low | (op<< 18 )  | (2 << 14) | offset );

			cmd_high = ( cmd_high | (0x7f << 16) |s_data_pair << 8 | s_base_pair );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("flat_store_dword_x2 "));

		}
        //static uint32_t get_base_pair_from_smem(std::vector<char>& instr) {
        //    assert(instr.size() == 8);
        //    uint32_t cmd_high = 0x0;
        //    std::memcpy(&cmd_high, instr.data() + 4, 4);

        //    return (cmd_high & 0xC0);  // Last six bits
        //}
        //static uint32_t get_data_pair_from_smem(std::vector<char>& instr) {
        //    assert(instr.size() == 8);
        //    uint32_t cmd_high = 0x0;
        //    std::memcpy(&cmd_high, instr.data() + 4, 4);

        //    return (cmd_high >> 6) & 0x80; // WRONG FLIPPED
        //}
        static uint32_t get_addr_from_flat(std::vector<char>& instr) {
            assert(instr.size() >= 4);
            uint32_t cmd_low = 0x0;
            std::memcpy(&cmd_low, instr.data(), 4);

            return (cmd_low & 0x00FF);
        }
		static MyInsn create_global_atomic_add( uint32_t s_data_pair, uint32_t s_base_pair  ,  uint32_t offset,vector<char *> & insn_pool ){
			uint32_t cmd_low = 0xde000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 66;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			cmd_low = (cmd_low | (op<< 18 )  | (2 << 14) | offset );

			cmd_high = ( cmd_high | (0x7f << 16) |s_data_pair << 8 | s_base_pair );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);



            printf("Creating atomic_add instr, cmd = %lx\n",*(uint64_t *)cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("flat_store_dword_x2 "));

		}


		static MyInsn create_global_atomic_inc( uint32_t s_data_pair, uint32_t s_base_pair  ,  uint32_t offset,vector<char *> & insn_pool ){
			uint32_t cmd_low = 0xde000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 75;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			cmd_low = (cmd_low | (op<< 18 )  | (2 << 14) | offset );

			cmd_high = ( cmd_high | (0x7f << 16) |s_data_pair << 8 | s_base_pair );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);

            printf("Creating atomic_inc instr, cmd = %lx\n",*(uint64_t *)cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("flat_store_dword_x2 "));

		}

		static MyInsn create_flat_store_dword_x2( uint32_t s_data_pair, uint32_t s_base_pair  ,  uint32_t offset,vector<char *> & insn_pool ){
			uint32_t cmd_low = 0xdc000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 29;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			cmd_low = (cmd_low | (op<< 18 )  | offset );

			cmd_high = ( cmd_high | s_data_pair << 8 | s_base_pair );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("flat_store_dword_x2 "));
		}
		static MyInsn create_flat_store_dword( uint32_t s_data_pair, uint32_t s_base_pair  ,  uint32_t offset,vector<char *> & insn_pool ){
			uint32_t cmd_low = 0xdc000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 28;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			cmd_low = (cmd_low | (op<< 18 )  | offset );

			cmd_high = ( cmd_high | s_data_pair << 8 | s_base_pair );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("flat_store_dword "));
		}
        // LDS
        //
		static MyInsn create_ds_write_b32( uint32_t vgpr_addr , uint32_t vgpr_data   ,vector<char *> & insn_pool ){
            uint32_t cmd_low = 0xda000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 13;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			cmd_low = (cmd_low | (op<< 17 )  );

			cmd_high = ( cmd_high | vgpr_data << 8 | vgpr_addr );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("flat_store_dword "));
		}
		static MyInsn create_ds_add_u32( uint32_t vgpr_addr , uint32_t vgpr_data   ,vector<char *> & insn_pool , uint32_t offset = 0){
            
            uint32_t cmd_low = 0xda000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 0;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			cmd_low = (cmd_low | (op<< 17 ) | offset );

			cmd_high = ( cmd_high | vgpr_data << 8 | vgpr_addr );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("flat_store_dword "));
		}
		static MyInsn create_ds_inc_u32( uint32_t vgpr_addr , uint32_t vgpr_data   ,vector<char *> & insn_pool , uint32_t offset = 0){
            uint32_t cmd_low = 0xda000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 3;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			cmd_low = (cmd_low | (op<< 17 ) | offset  );

			cmd_high = ( cmd_high | vgpr_data << 8 | vgpr_addr );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("flat_store_dword "));
		}

		static MyInsn create_ds_write_u32( uint32_t vgpr_addr , uint32_t vgpr_data   ,vector<char *> & insn_pool ){
            uint32_t cmd_low = 0xda000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 13;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			cmd_low = (cmd_low | (op<< 17 )  );

			cmd_high = ( cmd_high | vgpr_data << 8 | vgpr_addr );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("flat_store_dword "));
		}
		static MyInsn create_ds_write_b64( uint32_t vgpr_addr , uint32_t vgpr_data   ,vector<char *> & insn_pool , uint32_t offset = 0){
            uint32_t cmd_low = 0xda000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 77;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			cmd_low = (cmd_low | (op<< 17 ) | offset );

			cmd_high = ( cmd_high | vgpr_data << 8 | vgpr_addr );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("flat_store_dword "));
		}

		static MyInsn create_ds_read_b64( uint32_t vgpr_addr , uint32_t vdst   ,vector<char *> & insn_pool , uint32_t offset =0){
            uint32_t cmd_low = 0xda000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 118;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			cmd_low = (cmd_low | (op<< 17 )  | offset );

			cmd_high = ( cmd_high | vdst << 24 | vgpr_addr );
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("flat_store_dword "));
		}
		static MyInsn create_v_readlane_b32( uint32_t vdst , uint32_t src1 , uint32_t src0  ,vector<char *> & insn_pool){
            uint32_t cmd_low = 0xd0000000;
			uint32_t cmd_high = 0x0;
			uint32_t op = 649;
			char * cmd_ptr = (char *   ) malloc(sizeof(char) * 8 );
			cmd_low = (cmd_low | (op<< 16 )  | vdst );

			cmd_high = ( cmd_high | src1 << 9 | src0);
			memcpy( cmd_ptr ,&cmd_low,  4 );
			memcpy( cmd_ptr +4 ,&cmd_high,  4 );
			insn_pool.push_back(cmd_ptr);
			return MyInsn(cmd_ptr,8,std::string("flat_store_dword "));
		}
        static MyInsn create_v_writelane_b32( uint32_t vdst,  uint32_t src1, uint32_t src0, vector<char *> & insn_pool) {
            uint32_t cmd_low = 0xd0000000;
            uint32_t cmd_high = 0x0;
            uint32_t op = 0x28A;

            char* cmd_ptr = (char * ) malloc(sizeof(char) * 8);
            cmd_low = (cmd_low | (op << 16) | vdst );

            cmd_high = ( cmd_high | (src1 | 0x80) << 9 | src0);
            memcpy (cmd_ptr, &cmd_low, 4 );
            memcpy( cmd_ptr + 4, &cmd_high, 4);

            insn_pool.push_back(cmd_ptr);
            return MyInsn(cmd_ptr, 8, std::string("v_writelane "));
        }
};

#endif

