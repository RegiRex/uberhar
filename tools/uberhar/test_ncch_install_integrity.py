#!/usr/bin/env python3
"""CodexAstraLocal: Execute actual NCCH streaming methods and hash verifier with synthetic AES data."""
import argparse,hashlib,json,os,subprocess,tempfile
from pathlib import Path
from test_installer_security import definition,require,replace_once
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cxx',default='g++');p.add_argument('--linker');p.add_argument('--cryptopp-include',type=Path,required=True);p.add_argument('--cryptopp-library',type=Path,required=True);a=p.parse_args()
 base=ROOT/'build/uberhar-probe/ncch-install-integrity';base.mkdir(parents=True,exist_ok=True);out=Path(tempfile.mkdtemp(prefix='run-',dir=base));proof={'status':'INCOMPLETE','inputs':{},'variants':[]}
 def read(name):
  data=(ROOT/name).read_bytes();proof['inputs'][name]=hashlib.sha256(data).hexdigest();s=out/'sources'/name;s.parent.mkdir(parents=True,exist_ok=True);s.write_bytes(data);return data.decode()
 try:
  fixture=read('tools/uberhar/test_ncch_install_integrity.cpp');read('tools/uberhar/test_ncch_install_integrity.py');am=read('src/core/hle/service/am/am.cpp');header=read('src/core/hle/service/am/am.h');ncch=read('src/core/file_sys/ncch_container.h');read('src/core/file_sys/ncch_install_verifier.h')
  structures=ncch[ncch.index('struct NCSD_Partitions'):ncch.index('namespace FileSys')]
  cls=definition(header,'class NCCHCryptoFile final')+';';cls=cls.replace('private:','public:')
  # CodexAstraLocal: Preserve the production out-of-line owner destructor in this extracted class.
  destructor='NCCHCryptoFile::~NCCHCryptoFile() = default;'
  require(am.count(destructor)==1,'NCCH verifier destruction boundary changed')
  methods=destructor+'\n'+'\n'.join(definition(am,s) for s in ['void NCCHCryptoFile::WriteExact(','bool NCCHCryptoFile::Finalize(','void NCCHCryptoFile::Write('])
  variants=[('candidate',methods,None),('skip-inner-hash',replace_once(methods,'if (verifier && !verifier->Verify())','if (false)'),b'FAIL bad inner crypto rejected\n')]
  for label,body,expected in variants:
   case=out/label;case.mkdir();cpp=case/'fixture.cpp';obj=case/'fixture.o';exe=case/'fixture';cpp.write_text(fixture.replace('// @STRUCTURES@',structures).replace('// @CLASS@',cls).replace('// @METHODS@',body))
   cmd=[a.cxx,'-std=c++20','-O1','-g','-fsanitize=address','-fno-pie','-no-pie','-pthread','-I'+str(ROOT/'src'),'-I'+str(a.cryptopp_include),str(cpp)]
   cmd+=['-c','-o',str(obj)] if a.linker else [str(a.cryptopp_library),'-o',str(exe)]
   commands=[cmd]
   if a.linker:commands.append([a.linker,'-fsanitize=address','-no-pie','-pthread',str(obj),str(a.cryptopp_library),'-o',str(exe)])
   for i,command in enumerate(commands):
    result=subprocess.run(command,capture_output=True,timeout=120);(case/f'compile{i}.log').write_bytes(result.stdout+result.stderr);require(result.returncode==0,label+' compile/link failed')
   result=subprocess.run([str(exe)],capture_output=True,timeout=60,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'));(case/'run.log').write_bytes(result.stdout+result.stderr)
   require(result.returncode==0 if expected is None else result.returncode!=0 and expected in result.stderr,label+' behavioral result failed')
   proof['variants'].append({'name':label,'commands':commands,'exit':result.returncode,'stdout':result.stdout.decode(),'expected':expected.decode() if expected else None})
  proof['status']='PASS';print(out);print('PASS actual NCCH transform and inner-integrity negative control')
 finally:(out/'provenance.json').write_text(json.dumps(proof,indent=2)+'\n')
if __name__=='__main__':main()
