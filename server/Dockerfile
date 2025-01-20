FROM ruby:3.4.1
WORKDIR /app

# Install dependencies
RUN apt-get update -qq && apt-get install -y build-essential libpq-dev nodejs yarn postgresql-client

# Install gems
COPY Gemfile* /app/
RUN bundle install

# Copy the rest of the app
COPY . /app

# Precompile assets and prepare database (optional)
RUN rails assets:precompile

CMD ["rails", "server", "-b", "0.0.0.0"]