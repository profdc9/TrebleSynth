## Copyright (C) 2024 Daniel Marks
##
## zlib license...
##

## Author: Daniel Marks <Daniel Marks@VECTRON>
## Created: 2024-06-04

function retval = makewaves (n,fl)
  
if nargin<1
  n = 1024;
endif
if nargin<2
  fl='waves.c';
endif

maxone = @(x) x./max(abs(x));
subzero = @(x) (x-sum(x)/length(x));

fp=fopen(fl,'w');
fprintf(fp,'/* waves.c */\r\n\r\n#include <stdint.h>\r\n\r\n');

qmax=2^15-1;
ang=(0:n-1)*(2*pi/n);

sinwv = floor(sin(ang)*qmax+0.5);

rectwv = (sinwv>0).*sinwv;
rectwv = floor(maxone(subzero(rectwv))*qmax+0.5);

halfwv = sin((0:n-1)*(pi/n));
halfwv = floor(maxone(subzero(halfwv))*qmax+0.5);

triwv = ang * 0;
for n=0:1:2
  triwv = triwv + ((-1)^n)*(sin(ang*(2*n+1)))./((2*n+1).^2);
end
triwv = floor(maxone(subzero(triwv))*qmax+0.5);

sawwv = ang * 0;
for n=1:1:11
  sawwv = sawwv + -((-1)^n)*(sin(ang*n)./n);
end
sawwv = floor(maxone(subzero(sawwv))*qmax+0.5);

sqrwv = ang * 0;
for n=1:2:13
  sqrwv = sqrwv + (sin(ang*n)+1e-10)./n;
end
sqrwv = floor(maxone(subzero(sqrwv))*qmax+0.5);

sqrwv2 = ang * 0;
for n=1:2:17
  sqrwv2 = sqrwv2 + (sin((pi/8)*n)*cos((ang-pi)*n/2))/n;
end
sqrwv2 = floor(maxone(subzero(sqrwv2))*qmax+0.5);

sqrwv3 = ang * 0;
for n=1:2:33
  sqrwv3 = sqrwv3 + (sin((pi/16)*n)*cos((ang-pi)*n/2))/n;
end
sqrwv3 = floor(maxone(subzero(sqrwv3))*qmax+0.5);

wd1 = sin(ang)-sin(2*ang)+sin(3*ang)-sin(4*ang)+sin(5*ang);
wd1 = floor(maxone(subzero(wd1))*qmax+0.5);

wd2 = cos(ang)-sin(2*ang)/2+cos(3*ang)/3-sin(4*ang)/4+cos(5*ang)/5-sin(6*ang)/6+cos(7*ang)/7;
wd2 = floor(maxone(subzero(wd2))*qmax+0.5);

wd3 = sin(2*ang)/2-cos(3*ang)/3+sin(4*ang)/4+cos(5*ang)/5;
wd3 = floor(maxone(subzero(wd3))*qmax+0.5);

wd4 = -cos(ang)+cos(2*ang)/2-cos(3*ang)/3+cos(4*ang)/4-cos(5*ang)/5+cos(6*ang/6)-cos(7*ang)/7;
wd4 = floor(maxone(subzero(wd4))*qmax+0.5);

wd5 = cos(ang)-cos(3*ang)+sin(5*ang)-sin(7*ang)+cos(9*ang);
wd5 = floor(maxone(subzero(wd5))*qmax+0.5);

wd6 = (cos(ang*0.5).^2+1).^5;
wd6 = floor(maxone(subzero(wd6))*qmax+0.5);

wd7 = cos(ang)-cos(2*ang)/3+cos(4*ang)/5-cos(6*ang)/7+cos(8*ang)/9-cos(10*ang)/11;
wd7 = floor(maxone(subzero(wd7))*qmax+0.5);

wd8 = cos(ang)-cos(2*ang)*sqrt(2)+sin(3*ang)*sqrt(3)-sin(4*ang)*sqrt(4)+cos(5*ang)*sqrt(5);
wd8 = floor(maxone(subzero(wd8))*qmax+0.5);

writeary(fp,sinwv,'table_sine');
writeary(fp,halfwv,'table_halfsine');
writeary(fp,rectwv,'table_rectsine');
writeary(fp,triwv,'table_triangle');
writeary(fp,sawwv,'table_sawtooth');
writeary(fp,sqrwv,'table_squarewave');
writeary(fp,sqrwv2,'table_squarewave2');
writeary(fp,sqrwv3,'table_squarewave3');
writeary(fp,wd1,'table_wd1');
writeary(fp,wd2,'table_wd2');
writeary(fp,wd3,'table_wd3');
writeary(fp,wd4,'table_wd4');
writeary(fp,wd5,'table_wd5');
writeary(fp,wd6,'table_wd6');
writeary(fp,wd7,'table_wd7');
writeary(fp,wd8,'table_wd8');

fprintf(fp,'const int16_t *wavetables[16]= { table_sine, table_halfsine, table_rectsine, table_triangle, table_sawtooth, table_squarewave, table_squarewave2, table_squarewave3, table_wd1, table_wd2, table_wd3, table_wd4, table_wd5, table_wd6, table_wd7, table_wd8 };\r\n');
fclose(fp);

figure(1);clf;
subplot(4,4,1);
plot(ang,sinwv);
subplot(4,4,2);
plot(ang,rectwv);
subplot(4,4,3);
plot(ang,triwv);
subplot(4,4,4);
plot(ang,sawwv);
subplot(4,4,5);
plot(ang,sqrwv);
subplot(4,4,6);
plot(ang,sqrwv2);
subplot(4,4,7);
plot(ang,sqrwv3);
subplot(4,4,8);
plot(ang,halfwv);
subplot(4,4,9);
plot(ang,wd1);
subplot(4,4,10);
plot(ang,wd2);
subplot(4,4,11);
plot(ang,wd3);
subplot(4,4,12);
plot(ang,wd4);
subplot(4,4,13);
plot(ang,wd5);
subplot(4,4,14);
plot(ang,wd6);
subplot(4,4,15);
plot(ang,wd7);
subplot(4,4,16);
plot(ang,wd8);


endfunction


function x = writeary(fl,ary,name);
  
fprintf(fl,'const int16_t %s',name);
fprintf(fl,'[%d]={\r\n',
length(ary));
el=0;
for n=1:16:length(ary)-16
  fprintf(fl,'    ');
  fprintf(fl,'%d,',ary(n:n+15));
  el=el+16;
  fprintf(fl,'\r\n');
end
fprintf(fl,'    ');
fprintf(fl,'%d,',ary(length(ary)-15:length(ary)-1));
fprintf(fl,'%d', ary(length(ary)));
fprintf(fl,'};\r\n\r\n');
x=0;
endfunction
