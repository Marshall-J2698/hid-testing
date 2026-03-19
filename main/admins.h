const char* admin_IDs[] = {
    "13269892",
    "12345678"
};

bool is_admin(const char* adminList[], int len, const char* toCheck)
{
    for (int i = 0 ; i < len ; i++){
        if (strcmp(adminList[i],toCheck) == 0){// should not be hardcoded 9
            return true;
        }
    }
    return false;
}