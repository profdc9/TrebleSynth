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


writeary(fp,sinwv,'table_sine');
writeary(fp,halfwv,'table_halfsine');
writeary(fp,sqrwv,'table_square');
writeary(fp,triwv,'table_triangle');
writeary(fp,sawwv,'table_sawtooth');
writeary(fp,sqrwv,'table_squarewave');
writeary(fp,sqrwv2,'table_squarewave2');
writeary(fp,sqrwv3,'table_squarewave3');

fprintf(fp,'const int16_t *wavetables[8]= { table_sine, table_halfsine, table_square, table_triangle, table_sawtooth, table_squarewave, table_squarewave2, table_squarewave3 };\r\n');
fclose(fp);

figure(1);clf;
subplot(3,3,1);
plot(ang,sinwv);
subplot(3,3,2);
plot(ang,triwv);
subplot(3,3,3);
plot(ang,sawwv);
subplot(3,3,4);
plot(ang,sqrwv);
subplot(3,3,5);
plot(ang,sqrwv2);
subplot(3,3,6);
plot(ang,sqrwv3);
subplot(3,3,7);
plot(ang,halfwv);

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
