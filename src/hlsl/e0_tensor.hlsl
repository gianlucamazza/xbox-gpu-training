// E0 transformer primitives. mode=0 forward, 1..3 deterministic input gradients.
cbuffer Params : register(b0) {
  uint op, mode, count, rows, cols, outputCols, batch, seq, heads, aux;
  float epsilon;
};
StructuredBuffer<float> X : register(t0);
StructuredBuffer<float> W : register(t1);
StructuredBuffer<float> Z : register(t2);
StructuredBuffer<float> Y : register(t3);
StructuredBuffer<float> G : register(t4);
RWStructuredBuffer<float> Result : register(u0);

float Erf(float a) {
  float t=1/(1+0.3275911f*abs(a));
  float poly=(((((1.061405429f*t-1.453152027f)*t)+1.421413741f)*t-0.284496736f)*t+0.254829592f)*t;
  return sign(a)*(1-poly*exp(-a*a));
}
[numthreads(64,1,1)]
void CSMain(uint3 id:SV_DispatchThreadID) {
  uint n=id.x;
  if(op==12) {
    if(n>=rows) return;
    uint row=n,limit=row%seq;
    float maximum=-3.402823466e+38f,sum=0,dot=0;
    if(mode==0) {
      for(uint j=0;j<=limit;++j) maximum=max(maximum,X[row*seq+j]);
      for(uint j=0;j<=limit;++j) sum+=exp(X[row*seq+j]-maximum);
    } else for(uint j=0;j<=limit;++j) dot+=G[row*seq+j]*Y[row*seq+j];
    for(uint j=0;j<seq;++j) Result[row*seq+j]=j>limit?0:
      (mode?Y[row*seq+j]*(G[row*seq+j]-dot):exp(X[row*seq+j]-maximum)/sum);
    return;
  }
  if(n>=count) return;
  uint K=cols,O=outputCols,T=seq,H=heads,D=H?K/H:0;
  float v=0;
  if(op==0) v=mode?G[n]:X[n]+W[n];
  else if(op==9) v=mode?(mode==1?G[n]*W[n]:G[n]*X[n]):X[n]*W[n];
  else if(op==1) {
    if(mode==0) { uint r=n/O,o=n%O; for(uint k=0;k<K;++k) v+=X[r*K+k]*W[o*K+k]; }
    else if(mode==1) { uint r=n/K,k=n%K; for(uint o=0;o<O;++o) v+=G[r*O+o]*W[o*K+k]; }
    else { uint o=n/K,k=n%K; for(uint r=0;r<rows;++r) v+=G[r*O+o]*X[r*K+k]; }
  } else if(op==2) {
    if(mode==2) {
      uint c=n;
      for(uint r=0;r<rows;++r) { float ss=0; for(uint k=0;k<K;++k) ss+=X[r*K+k]*X[r*K+k]; v+=G[r*K+c]*X[r*K+c]/sqrt(ss/K+epsilon); }
    } else {
      uint r=n/K,c=n%K; float ss=0,dot=0;
      for(uint k=0;k<K;++k) { ss+=X[r*K+k]*X[r*K+k]; if(mode) dot+=G[r*K+k]*W[k]*X[r*K+k]; }
      float inv=1/sqrt(ss/K+epsilon);
      v=mode?inv*(G[n]*W[c]-X[n]*dot*inv*inv/K):X[n]*inv*W[c];
    }
  } else if(op==3) {
    uint c=n%K,row=n/K,pos=row%T,pair=c-c%2;
    float angle=float(pos)*pow(10000.0f,-float(pair)/K);
    uint other=c%2?n-1:n+1;
    v=mode?(G[n]*cos(angle)+(c%2?-G[other]:G[other])*sin(angle)):
      (X[n]*cos(angle)+(c%2?X[other]:-X[other])*sin(angle));
  } else if(op==4) {
    float a=X[n],f=0,g=0;
    if(aux==0) { f=0.5f*a*(1+Erf(a*0.7071067811865475f)); g=0.5f*(1+Erf(a*0.7071067811865475f))+a*0.3989422804014327f*exp(-0.5f*a*a); }
    else if(aux==1) { f=max(a,0)*max(a,0); g=2*max(a,0); }
    else { float sig=1/(1+exp(-a)); f=a*sig; g=sig+a*sig*(1-sig); }
    v=mode?G[n]*g:f;
  } else if(op==5) {
    if(mode==0) { uint c=n%D,row=n/D,t=row%T,h=(row/T)%H,b=row/(T*H); v=X[(b*T+t)*3*K+aux*K+h*D+c]; }
    else { uint c=n%(3*K),r=n/(3*K),b=r/T,t=r%T; if(c/K==aux) { c%=K; uint h=c/D; v=G[((b*H+h)*T+t)*D+c%D]; } }
  } else if(op==6) {
    if(mode==0) { uint c=n%K,r=n/K,b=r/T,t=r%T,h=c/D; v=X[((b*H+h)*T+t)*D+c%D]; }
    else { uint c=n%D,row=n/D,t=row%T,h=(row/T)%H,b=row/(T*H); v=G[(b*T+t)*K+h*D+c]; }
  } else if(op==7) {
    if(mode==0) v=W[uint(X[n/K])*K+n%K];
    else if(mode==2) { uint token=n/K,c=n%K; for(uint r=0;r<rows;++r) if(uint(X[r])==token) v+=G[r*K+c]; }
  } else if(op==8) {
    if(mode==0) { uint r=n/O,c=n%O; v=X[r*K+aux*O+c]; }
    else { uint r=n/K,c=n%K; if(c/O==aux) v=G[r*O+c%O]; }
  } else if(op==11) {
    if(mode==0) { uint row=n/T,key=n%T,base=row-row%T;for(uint c=0;c<K;++c) v+=X[row*K+c]*W[(base+key)*K+c]; }
    else if(mode==1) { uint row=n/K,c=n%K,base=row-row%T;for(uint j=0;j<T;++j) v+=G[row*T+j]*W[(base+j)*K+c]; }
    else { uint row=n/K,c=n%K,key=row%T,base=row-key;for(uint i=0;i<T;++i) v+=G[(base+i)*T+key]*X[(base+i)*K+c]; }
    v/=sqrt(float(K));
  } else if(op==13) {
    if(mode==0) { uint row=n/K,c=n%K,base=row-row%T;for(uint j=0;j<=row%T;++j) v+=X[row*T+j]*W[(base+j)*K+c]; }
    else if(mode==1) { uint row=n/T,key=n%T,base=row-row%T;if(key<=row%T) for(uint c=0;c<K;++c) v+=G[row*K+c]*W[(base+key)*K+c]; }
    else { uint row=n/K,c=n%K,key=row%T,base=row-key;for(uint i=key;i<T;++i) v+=X[(base+i)*T+key]*G[(base+i)*K+c]; }
  } else if(op==10) {
    uint r=mode?n/K:n,c=n%K; float maximum=-3.402823466e+38f,sum=0;
    for(uint k=0;k<K;++k) maximum=max(maximum,X[r*K+k]);
    for(uint k=0;k<K;++k) sum+=exp(X[r*K+k]-maximum);
    v=mode?G[r]*(exp(X[n]-maximum)/sum-(uint(W[r])==c?1:0)):
      log(sum)-(X[r*K+uint(W[r])]-maximum);
  }
  Result[n]=v;
}
