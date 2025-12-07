#include<stdio.h>
int y[3][3][3] = { 1, 2, 0, 0, 0, 0, 0, { 3, 4, 5 }, 6, 7 };
int main(){
    for(int i=0;i<3;i++){
        for(int j=0;j<3;j++){
            for(int k=0;k<3;k++){
                printf("%d ",y[i][j][k]);
            }
            printf("\n");
        }
        printf("\n");
    }
    return 0;
}