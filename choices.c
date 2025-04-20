choices_t* choices_create(size_t capacity) {
    choices_t* choices = malloc(sizeof(choices_t));
    if (!choices) {
        return NULL;
    }

    choices->values = calloc(capacity, sizeof(iso_data_t*));
    if (!choices->values) {
        free(choices);
        return NULL;
    }

    choices->len = 0;
    choices->cap = capacity;
    choices->cur = 0;
    return choices;
}

bool choices_append(choices_t* choices, iso_data_t* iso_data) {
    if (!choices || !iso_data) {
        return false;
    }

    if(choices->len >= choices->cap) {
        return false;
    }

    choices->values[choices->len++] = iso_data;
    return true;
}

void choices_free(choices_t* choices) {
    if (!choices) {
        return;
    }
    
    if (choices->values) {
        for (int i = 0; i < choices->len; i++) {
            if (choices->values[i]) {
                free(choices->values[i]->label);
                free(choices->values[i]->url);
                free(choices->values[i]->content_id);
                free(choices->values[i]->sha256sum);
                free(choices->values[i]);
            }
        }
        free(choices->values);
    }
    
    free(choices);
}